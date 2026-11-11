//! Device verification (emoji) and device management.
//! (part of `impl Core`, split out of core.rs; the items it uses live there)

use super::*;

impl Core {
    /// Incoming verification requests, to-device and in-room.
    pub(crate) fn install_handlers(&'static self, client: &Client) {
        client.add_event_handler(|ev: ToDeviceKeyVerificationRequestEvent, client: Client| async move {
            // spawned: a handler that awaits the whole flow would hold up the sync
            if let Some(req) = client
                .encryption()
                .get_verification_request(&ev.sender, ev.content.transaction_id.as_str())
                .await
            {
                tokio::spawn(async move { crate::instance().watch_request(req).await });
            }
        });
        client.add_event_handler(|ev: OriginalSyncRoomMessageEvent, client: Client| async move {
            if let MessageType::VerificationRequest(_) = &ev.content.msgtype {
                if let Some(req) =
                    client.encryption().get_verification_request(&ev.sender, ev.event_id.as_str()).await
                {
                    tokio::spawn(async move { crate::instance().watch_request(req).await });
                }
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
                        self.state.lock().await.sas.insert(flow.clone(), sas.clone());
                        self.rt.spawn(self.watch_sas(flow.clone(), sas));
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

    pub(crate) async fn request(&self, v: &Value) -> Result<VerificationRequest> {
        let flow = s(v, "flow_id")?;
        self.state.lock().await.requests.get(&flow).cloned().context("no such verification")
    }

    pub(crate) async fn sas(&self, v: &Value) -> Result<SasVerification> {
        let flow = s(v, "flow_id")?;
        self.state.lock().await.sas.get(&flow).cloned().context("no emoji verification in progress")
    }

    pub(crate) async fn delete_device(&'static self, v: &Value) -> Result<()> {
        let client = self.client().await?;
        let id: OwnedDeviceId = s(v, "device_id")?.into();
        let password = s(v, "password")?;
        let ids = [id];
        if let Err(e) = client.delete_devices(&ids, None).await {
            let info = e.as_uiaa_response().context(format!("{e}"))?;
            let localpart = client.user_id().context("no user id")?.localpart().to_owned();
            let mut auth = Password::new(UserIdentifier::Matrix(MatrixUserIdentifier::new(localpart)), password);
            auth.session = info.session.clone();
            client.delete_devices(&ids, Some(AuthData::Password(auth))).await?;
        }
        self.emit(json!({"ev": "device_deleted", "device_id": ids[0].as_str()}));
        Ok(())
    }
}
