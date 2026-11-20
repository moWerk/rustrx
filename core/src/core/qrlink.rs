// SPDX-License-Identifier: GPL-3.0-or-later
//! QR codes between two devices (MSC4108): a code the camera read, and
//! letting a new device in from this logged-in one ("Link a new device").
//! The logged-in device hands the new one its keys, so it comes in
//! verified. (part of `impl Core`; the login that shows its own code is
//! login_qr in session.rs)

use super::*;
use matrix_sdk::authentication::oauth::qrcode::{
    GeneratedQrProgress, GrantLoginProgress, LoginProgress, Msc4108IntentData, QrCodeData, QrCodeIntent,
    QrCodeIntentData, QrProgress,
};
use std::future::IntoFuture;

/// A code to draw: the event fields the QR pages paint from.
pub(crate) fn qr_modules(data: &QrCodeData) -> Result<serde_json::Value> {
    let code = qrcode::QrCode::new(data.to_bytes())?;
    let modules: String = code.to_colors().iter().map(|c| if *c == qrcode::Color::Dark { '1' } else { '0' }).collect();
    Ok(json!({"width": code.width(), "modules": modules}))
}

/// How Rustrx registers with the account service for a QR login: a native
/// app on the device-code grant.
pub(crate) fn device_code_registration() -> Result<matrix_sdk::authentication::oauth::ClientRegistrationData> {
    use matrix_sdk::authentication::oauth::registration::{ApplicationType, ClientMetadata, Localized, OAuthGrantType};
    use matrix_sdk::reqwest::Url;
    let mut meta = ClientMetadata::new(
        ApplicationType::Native,
        vec![OAuthGrantType::DeviceCode],
        Localized::new(Url::parse(CLIENT_URI)?, []),
    );
    meta.client_name = Some(Localized::new(DEVICE_NAME.to_owned(), []));
    Ok(matrix_sdk::authentication::oauth::ClientRegistrationData::new(matrix_sdk::ruma::serde::Raw::new(&meta)?))
}

impl Core {
    /// A QR login makes a new device with a new key every time, so the
    /// store of this folder can never be used again: a QR login that got
    /// as far as the store and then failed left its device behind, and the
    /// next one was refused ("the account in the store doesn't match the
    /// account in the constructor", jp26). A store kept for a relogin
    /// (messages and keys of a session the server ended) is not thrown
    /// away; that login needs the password or the browser.
    pub(crate) fn fresh_store_for_qr(&self) -> Result<()> {
        if self.relogin_path().exists() {
            bail!("this account's messages and keys are still on this phone; log in with the password or in the browser to keep them");
        }
        let dir = self.data_dir.join(STORE_DIR);
        if std::fs::read_dir(&dir).map(|mut d| d.next().is_some()).unwrap_or(false) {
            tracing::info!("qr: the store of an earlier login attempt goes");
            std::fs::remove_dir_all(&dir)?;
        }
        std::fs::remove_file(self.data_dir.join(DEVICE_ID_FILE)).ok();
        Ok(())
    }

    /// A QR step for the UI ("qr_login" or "qr_grant"), and for the log:
    /// the steps' timing is what a failed QR login is read from (the
    /// rendezvous lives a minute)
    pub(crate) fn qr_step(&self, ev: serde_json::Value) {
        tracing::info!(
            kind = ev["ev"].as_str(),
            state = ev["state"].as_str(),
            reason = ev["reason"].as_str(),
            message = ev["message"].as_str(),
            "qr: step"
        );
        self.emit(ev);
    }

    /// A code the camera read (base64 of its bytes). Logged in, it is a
    /// new device's code, which this one lets in ("qr_grant"); logged out,
    /// a logged-in device's code to log in with ("qr_login"). A code of
    /// the other kind is refused with what to scan instead.
    pub(crate) async fn qr_scanned(&'static self, data: &str) -> Result<()> {
        let logged_in = self.state.lock().await.client.is_some();
        let code = match QrCodeData::from_base64(data) {
            Ok(code) => code,
            Err(e) => {
                tracing::info!("qr: not a login code: {e}");
                self.qr_refused(logged_in, "not_login");
                return Ok(());
            }
        };
        tracing::info!(logged_in, intent = ?code.intent(), "qr: a code was scanned");
        match (logged_in, code.intent()) {
            (true, QrCodeIntent::Login) => self.grant_scanned(code).await,
            (false, QrCodeIntent::Reciprocate) => self.login_scanned(code).await,
            (true, QrCodeIntent::Reciprocate) => {
                self.qr_refused(true, "logged_in_code");
                Ok(())
            }
            (false, QrCodeIntent::Login) => {
                self.qr_refused(false, "new_device_code");
                Ok(())
            }
        }
    }

    /// A scanned code that cannot be used here; the page says why from
    /// `reason` and the camera keeps looking.
    fn qr_refused(&self, logged_in: bool, reason: &str) {
        let ev = if logged_in { "qr_grant" } else { "qr_login" };
        self.emit(json!({"ev": ev, "state": "refused", "reason": reason}));
    }

    /// Logging in with the code a logged-in device shows: this device
    /// shows two digits for the other one, which then confirms the login
    /// and hands over the keys. Ends in logged_in or login_error.
    async fn login_scanned(&'static self, code: QrCodeData) -> Result<()> {
        let Some(_login) = self.login_or_refuse() else { return Ok(()) };
        let cancelled = self.sso_cancel.notified();
        tokio::pin!(cancelled);
        let result: Result<Client> = async {
            let server = match code.intent_data() {
                QrCodeIntentData::Msc4108 { data: Msc4108IntentData::Reciprocate { server_name }, .. } => server_name.clone(),
                QrCodeIntentData::Msc4388 { base_url, .. } => base_url.to_string(),
                QrCodeIntentData::Msc4108 { .. } => bail!("the code names no server"),
            };
            self.qr_step(json!({"ev": "qr_login", "state": "connecting"}));
            self.fresh_store_for_qr()?;
            let client = self.build_client(&server, true).await.map_err(|e| anyhow!("{server}: {e}"))?;
            let registration = device_code_registration()?;
            let oauth = client.oauth();
            let login = oauth.login_with_qr_code(Some(&registration)).scan(&code);
            let mut progress = login.subscribe_to_progress();
            let done = login.into_future();
            tokio::pin!(done);
            let deadline = tokio::time::sleep(SSO_TIMEOUT);
            tokio::pin!(deadline);
            let no_answer = tokio::time::sleep(QR_CHANNEL_TIMEOUT);
            tokio::pin!(no_answer);
            let mut answered = false;
            loop {
                tokio::select! {
                    r = &mut done => {
                        r.map_err(|e| anyhow!("{e}"))?;
                        break;
                    }
                    Some(p) = progress.next() => match p {
                        LoginProgress::EstablishingSecureChannel(QrProgress { check_code }) => {
                            answered = true;
                            self.qr_step(json!({"ev": "qr_login", "state": "check", "check_code": format!("{check_code:02}")}));
                        }
                        LoginProgress::WaitingForToken { user_code } => {
                            self.qr_step(json!({"ev": "qr_login", "state": "waiting", "user_code": user_code}));
                        }
                        LoginProgress::SyncingSecrets => self.qr_step(json!({"ev": "qr_login", "state": "syncing"})),
                        LoginProgress::Done => self.qr_step(json!({"ev": "qr_login", "state": "done"})),
                        _ => {}
                    },
                    _ = &mut cancelled => bail!("login cancelled"),
                    _ = &mut no_answer, if !answered => {
                        tracing::info!("qr: no answer from the other device");
                        bail!("the other device did not answer within a minute. Its server may keep QR logins from working (a proxy that compresses the rendezvous)")
                    }
                    _ = &mut deadline => bail!("no login from the other device within {} minutes", SSO_TIMEOUT.as_secs() / 60),
                }
            }
            std::fs::remove_file(self.data_dir.join(DEVICE_ID_FILE)).ok();
            std::fs::remove_file(self.relogin_path()).ok();
            Ok(client)
        }
        .await;
        match result {
            Ok(client) => self.after_login(client).await,
            Err(e) => {
                self.emit(json!({"ev": "login_error", "message": format!("{e:#}")}));
                Ok(())
            }
        }
    }

    /// Letting in the new device whose code was scanned: this device shows
    /// two digits for the new one ("check").
    async fn grant_scanned(&'static self, code: QrCodeData) -> Result<()> {
        let Some(_busy) = self.begin_login() else { return self.grant_busy() };
        let client = self.client().await?;
        let oauth = client.oauth();
        self.qr_step(json!({"ev": "qr_grant", "state": "connecting"}));
        let grant = oauth.grant_login_with_qr_code().scan(&code);
        let progress = grant.subscribe_to_progress();
        self.run_grant(grant.into_future(), progress, true, |QrProgress { check_code }| async move {
            self.qr_step(json!({"ev": "qr_grant", "state": "check", "check_code": format!("{check_code:02}")}));
        })
        .await
    }

    /// Letting in a new device that scans the code this one shows ("qr");
    /// the new device then shows two digits, typed here ("scanned", then
    /// qr_check_code).
    pub(crate) async fn grant_qr(&'static self) -> Result<()> {
        let Some(_busy) = self.begin_login() else { return self.grant_busy() };
        let client = self.client().await?;
        let oauth = client.oauth();
        let grant = oauth.grant_login_with_qr_code().generate();
        let progress = grant.subscribe_to_progress();
        let result = self
            .run_grant(grant.into_future(), progress, false, |p| async move {
                match p {
                    GeneratedQrProgress::QrReady(data) => match qr_modules(&data) {
                        Ok(mut ev) => {
                            ev["ev"] = json!("qr_grant");
                            ev["state"] = json!("qr");
                            self.qr_step(ev);
                        }
                        Err(e) => tracing::warn!("qr: cannot draw the code: {e}"),
                    },
                    GeneratedQrProgress::QrScanned(sender) => {
                        *self.qr_check.lock().await = Some(sender);
                        self.qr_step(json!({"ev": "qr_grant", "state": "scanned"}));
                    }
                }
            })
            .await;
        self.qr_check.lock().await.take();
        result
    }

    fn grant_busy(&self) -> Result<()> {
        self.qr_step(json!({"ev": "qr_grant", "state": "error", "message": "another login is still running"}));
        Ok(())
    }

    /// The part both ways of letting a device in share: the secure channel
    /// (`channel` handles its progress), the account service's consent page
    /// in the browser, the keys going over. Ends in "done" or "error";
    /// login_cancel ends it early. `scanned`: this device scanned, so the
    /// other one answers at once or not at all (QR_CHANNEL_TIMEOUT).
    async fn run_grant<Q, F, Fut>(
        &'static self,
        done: impl std::future::Future<Output = Result<(), matrix_sdk::authentication::oauth::qrcode::QRCodeGrantLoginError>>,
        progress: impl futures_util::Stream<Item = GrantLoginProgress<Q>>,
        scanned: bool,
        channel: F,
    ) -> Result<()>
    where
        F: Fn(Q) -> Fut,
        Fut: std::future::Future<Output = ()>,
    {
        let cancelled = self.sso_cancel.notified();
        tokio::pin!(cancelled, done, progress);
        let deadline = tokio::time::sleep(SSO_TIMEOUT);
        tokio::pin!(deadline);
        let no_answer = tokio::time::sleep(QR_CHANNEL_TIMEOUT);
        tokio::pin!(no_answer);
        let mut answered = !scanned;
        let result: Result<()> = loop {
            tokio::select! {
                r = &mut done => break r.map_err(|e| anyhow!("{e}")),
                Some(p) = progress.next() => match p {
                    GrantLoginProgress::EstablishingSecureChannel(q) => {
                        answered = true;
                        channel(q).await
                    }
                    GrantLoginProgress::WaitingForAuth { verification_uri, continuation_sender } => {
                        // the account service asks in the browser whether
                        // the new device may in; the grant goes on meanwhile
                        self.qr_step(json!({"ev": "qr_grant", "state": "consent"}));
                        self.emit(json!({"ev": "open_url", "url": verification_uri.as_str()}));
                        if let Err(e) = continuation_sender.confirm().await {
                            break Err(anyhow!("{e}"));
                        }
                    }
                    GrantLoginProgress::SyncingSecrets => self.qr_step(json!({"ev": "qr_grant", "state": "syncing"})),
                    GrantLoginProgress::Done | GrantLoginProgress::Starting => {}
                },
                _ = &mut cancelled => break Err(anyhow!("cancelled")),
                _ = &mut no_answer, if !answered => {
                    tracing::info!("qr: no answer from the new device");
                    break Err(anyhow!("the new device did not answer within a minute. Its server may keep QR logins from working (a proxy that compresses the rendezvous)"));
                }
                _ = &mut deadline => break Err(anyhow!("the new device did not finish within {} minutes", SSO_TIMEOUT.as_secs() / 60)),
            }
        };
        match result {
            Ok(()) => {
                tracing::info!("qr: a new device was let in");
                self.qr_step(json!({"ev": "qr_grant", "state": "done"}));
            }
            Err(e) => {
                tracing::info!("qr: letting a device in failed: {e:#}");
                self.qr_step(json!({"ev": "qr_grant", "state": "error", "message": format!("{e:#}")}));
            }
        }
        Ok(())
    }
}
