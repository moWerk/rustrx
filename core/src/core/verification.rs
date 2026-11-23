// SPDX-License-Identifier: GPL-3.0-or-later
//! Device verification (emoji).
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;
use matrix_sdk::ruma::serde::Raw;

/// An emoji comparison's state for the log, without its contents.
fn sas_state_name(state: &SasState) -> &'static str {
    match state {
        SasState::Created { .. } => "created",
        SasState::Started { .. } => "started",
        SasState::Accepted { .. } => "accepted",
        SasState::KeysExchanged { .. } => "keys_exchanged",
        SasState::Confirmed => "confirmed",
        SasState::Done { .. } => "done",
        SasState::Cancelled(_) => "cancelled",
    }
}

/// A request's state for the log, without its contents.
fn request_state_name(state: &VerificationRequestState) -> &'static str {
    match state {
        VerificationRequestState::Created { .. } => "created",
        VerificationRequestState::Requested { .. } => "requested",
        VerificationRequestState::Ready { .. } => "ready",
        VerificationRequestState::Transitioned { .. } => "transitioned",
        VerificationRequestState::Done => "done",
        VerificationRequestState::Cancelled(_) => "cancelled",
    }
}

/// The sender and event id of an in-room verification request, read from
/// the raw message without deserializing it whole.
fn verification_request_of(raw: &Raw<OriginalSyncRoomMessageEvent>) -> Option<(OwnedUserId, String)> {
    #[derive(Deserialize)]
    struct Content {
        msgtype: Option<String>,
    }
    if raw.get_field::<String>("type").ok().flatten().as_deref() != Some("m.room.message") {
        return None;
    }
    let content = raw.get_field::<Content>("content").ok().flatten()?;
    if content.msgtype.as_deref() != Some("m.key.verification.request") {
        return None;
    }
    let sender = OwnedUserId::try_from(raw.get_field::<String>("sender").ok().flatten()?).ok()?;
    Some((sender, raw.get_field::<String>("event_id").ok().flatten()?))
}

impl Core {
    /// Incoming verification requests, to-device and in-room.
    pub(crate) fn install_handlers(&'static self, client: &Client) {
        client.add_event_handler(move |ev: ToDeviceKeyVerificationRequestEvent, client: Client| async move {
            // spawned: a handler that awaits the whole flow would hold up the sync
            if let Some(req) = client
                .encryption()
                .get_verification_request(&ev.sender, ev.content.transaction_id.as_str())
                .await
            {
                let core = self;
                core.spawn_login_task(core.watch_request(req)).await;
            }
        });
        // raw: every m.room.message of every sync comes here, and only a
        // request is worth deserializing (the SDK reads it itself)
        client.add_event_handler(move |raw: Raw<OriginalSyncRoomMessageEvent>, client: Client| async move {
            let Some((sender, event_id)) = verification_request_of(&raw) else { return };
            if let Some(req) = client.encryption().get_verification_request(&sender, &event_id).await {
                let core = self;
                core.spawn_login_task(core.watch_request(req)).await;
            }
        });
    }

    pub(crate) fn request_json(req: &VerificationRequest, state: &str, extra: Value) -> Value {
        let mut v = json!({
            "ev": "verification",
            "flow_id": req.flow_id(),
            "state": state,
            "other_user": req.other_user_id().as_str(),
            "we_started": req.we_started(),
            "self_verification": req.is_self_verification(),
        });
        if let (Value::Object(o), Value::Object(e)) = (&mut v, extra) {
            o.extend(e);
        }
        v
    }

    pub(crate) async fn watch_request(&'static self, req: VerificationRequest) {
        let flow = req.flow_id().to_owned();
        {
            let mut st = self.state.lock().await;
            if st.requests.contains_key(&flow) {
                return;
            }
            st.requests.insert(flow.clone(), req.clone());
        }
        let mut changes = req.changes();
        let mut current = Some(req.state());
        loop {
            let Some(state) = current.take() else { break };
            tracing::info!(%flow, we_started = req.we_started(), state = request_state_name(&state), "verification");
            let done = match &state {
                VerificationRequestState::Created { .. } => {
                    self.emit(Self::request_json(&req, "created", json!({})));
                    false
                }
                VerificationRequestState::Requested { other_device_data, .. } => {
                    self.emit(Self::request_json(
                        &req,
                        "requested",
                        json!({"other_device": other_device_data.device_id().as_str(),
                               "other_device_name": other_device_data.display_name()}),
                    ));
                    false
                }
                VerificationRequestState::Ready { other_device_data, .. } => {
                    self.emit(Self::request_json(
                        &req,
                        "ready",
                        json!({"other_device": other_device_data.device_id().as_str(),
                               "other_device_name": other_device_data.display_name()}),
                    ));
                    // The requester drives the SAS flow; the other side waits.
                    if req.we_started() {
                        if let Err(e) = req.start_sas().await {
                            self.error("verification_start_sas", &anyhow!("{e}"));
                        }
                    }
                    false
                }
                VerificationRequestState::Transitioned { verification } => {
                    if let Some(sas) = verification.clone().sas() {
                        self.emit(Self::request_json(&req, "sas", json!({})));
                        let mut st = self.state.lock().await;
                        st.sas.insert(flow.clone(), sas.clone());
                        st.login_tasks.push(self.rt.spawn(self.watch_sas(flow.clone(), sas)));
                    } else {
                        self.emit(Self::request_json(&req, "unsupported", json!({})));
                    }
                    false
                }
                VerificationRequestState::Done => {
                    self.emit(Self::request_json(&req, "done", json!({})));
                    true
                }
                VerificationRequestState::Cancelled(info) => {
                    self.emit(Self::request_json(
                        &req,
                        "cancelled",
                        json!({"reason": info.reason(), "by_us": info.cancelled_by_us()}),
                    ));
                    true
                }
            };
            if done {
                break;
            }
            current = changes.next().await;
        }
        let mut st = self.state.lock().await;
        st.requests.remove(&flow);
        st.sas.remove(&flow);
    }

    pub(crate) async fn watch_sas(&'static self, flow: String, sas: SasVerification) {
        let mut changes = sas.changes();
        let mut current = Some(sas.state());
        while let Some(state) = current.take() {
            // every step with who started it: a comparison that stalls is
            // read from these (dodoradio's on Beeper stopped without a trace)
            tracing::info!(%flow, we_started = sas.we_started(), state = sas_state_name(&state), "sas");
            match state {
                SasState::KeysExchanged { emojis: state_emojis, decimals: state_decimals } => {
                    // the state carries them; the accessor is only a fallback
                    let list = state_emojis.map(|e| e.emojis).or_else(|| sas.emoji());
                    let emojis: Vec<Value> = list
                        .map(|e| e.iter().map(|e| json!({"symbol": e.symbol, "description": e.description})).collect())
                        .unwrap_or_default();
                    let (a, b, c) = state_decimals;
                    let decimals = Some(vec![a, b, c]);
                    tracing::info!(%flow, emojis = emojis.len(), "sas keys exchanged");
                    self.emit(json!({"ev": "sas", "flow_id": flow, "state": "keys_exchanged",
                                     "emojis": emojis, "decimals": decimals}));
                }
                // the other side started the comparison (Element's "Start" after
                // the request was accepted here): agree to it, or both wait.
                // Only the emojis that follow decide; nothing is trusted yet
                SasState::Started { .. } if !sas.we_started() => {
                    tracing::info!(%flow, "sas started by the other side, accepting");
                    if let Err(e) = sas.accept().await {
                        self.error("verification_accept", &anyhow!("{e}"));
                    }
                }
                SasState::Confirmed => {
                    self.emit(json!({"ev": "sas", "flow_id": flow, "state": "confirmed"}));
                }
                SasState::Done { .. } => {
                    self.emit(json!({"ev": "sas", "flow_id": flow, "state": "done"}));
                    break;
                }
                SasState::Cancelled(info) => {
                    self.emit(json!({"ev": "sas", "flow_id": flow, "state": "cancelled",
                                     "reason": info.reason()}));
                    break;
                }
                _ => {}
            }
            current = changes.next().await;
        }
    }

    pub(crate) async fn request(&self, flow: &str) -> Result<VerificationRequest> {
        self.state.lock().await.requests.get(flow).cloned().context("no such verification")
    }

    pub(crate) async fn sas(&self, flow: &str) -> Result<SasVerification> {
        self.state.lock().await.sas.get(flow).cloned().context("no emoji verification in progress")
    }

    /// Verifying this device with another of the own ones; the flow then
    /// goes on as "verification" events.
    pub(crate) async fn verification_request_self(&'static self) -> Result<()> {
        let client = self.client().await?;
        let own = client.user_id().context("no user id")?.to_owned();
        let identity = client
            .encryption()
            .get_user_identity(&own)
            .await?
            .context("own identity not known yet; wait for the first sync")?;
        let req = identity.request_verification().await?;
        self.spawn_login_task(self.watch_request(req)).await;
        Ok(())
    }

    /// Verifying someone else: a request in a direct chat with them (the
    /// SDK finds or makes it), the same emoji flow as for one's own devices.
    pub(crate) async fn verify_user(&'static self, user_id: &str) -> Result<()> {
        let client = self.client().await?;
        let user = UserId::parse(user_id.trim())?;
        let identity = client
            .encryption()
            .get_user_identity(&user)
            .await?
            .context("they have not set up verification (no cross-signing keys)")?;
        let req = identity.request_verification().await?;
        self.spawn_login_task(self.watch_request(req)).await;
        Ok(())
    }

    pub(crate) async fn verification_accept(&self, flow: &str) -> Result<()> {
        self.request(flow).await?.accept().await?;
        Ok(())
    }

    pub(crate) async fn verification_start_sas(&self, flow: &str) -> Result<()> {
        self.request(flow).await?.start_sas().await?;
        Ok(())
    }

    pub(crate) async fn verification_confirm(&self, flow: &str) -> Result<()> {
        self.sas(flow).await?.confirm().await?;
        Ok(())
    }

    pub(crate) async fn verification_mismatch(&self, flow: &str) -> Result<()> {
        self.sas(flow).await?.mismatch().await?;
        Ok(())
    }

    pub(crate) async fn verification_cancel(&self, flow: &str) -> Result<()> {
        self.request(flow).await?.cancel().await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn raw(v: Value) -> Raw<OriginalSyncRoomMessageEvent> {
        Raw::new(&v).unwrap().cast_unchecked()
    }

    /// Only an in-room request is picked out, from its raw fields; plain
    /// messages, other types and a redacted request are left alone.
    /// Planted-bug: drop the msgtype check (every message is looked up as
    /// a request).
    #[test]
    fn only_verification_requests_are_picked_out() {
        let request = json!({"type": "m.room.message", "event_id": "$req", "sender": "@a:x", "origin_server_ts": 1,
                             "content": {"msgtype": "m.key.verification.request", "body": "", "to": "@b:x",
                                         "from_device": "D", "methods": ["m.sas.v1"]}});
        assert_eq!(verification_request_of(&raw(request.clone())), Some((OwnedUserId::try_from("@a:x").unwrap(), "$req".into())));
        let mut text = request.clone();
        text["content"] = json!({"msgtype": "m.text", "body": "hi"});
        assert_eq!(verification_request_of(&raw(text)), None);
        let mut other = request.clone();
        other["type"] = json!("m.sticker");
        assert_eq!(verification_request_of(&raw(other)), None);
        let mut redacted = request;
        redacted["content"] = json!({});
        redacted["unsigned"] = json!({"redacted_because": {"type": "m.room.redaction"}});
        assert_eq!(verification_request_of(&raw(redacted)), None);
    }
}
