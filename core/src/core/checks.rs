// SPDX-License-Identifier: GPL-3.0-or-later
//! Refusals the UI words itself (0.8.4 audit): a plain http server, a
//! consent page on another host, a browser login as someone else; and when a
//! session the server ended counts as gone for good. Pure where it can be,
//! so the tests below hold the rules.

use std::net::IpAddr;

use serde_json::{json, Value};

/// An error the UI says in its own words: `reason` and `fields` go out with
/// the event (login_error, error, qr_grant), `message` is for the log.
#[derive(Debug)]
pub(crate) struct Refusal {
    pub(crate) reason: &'static str,
    pub(crate) fields: Value,
    pub(crate) message: String,
}

impl std::fmt::Display for Refusal {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for Refusal {}

/// `ev` with the refusal's reason and fields, when `err` is one.
pub(crate) fn with_refusal(mut ev: Value, err: &anyhow::Error) -> Value {
    if let Some(r) = err.downcast_ref::<Refusal>() {
        ev["reason"] = json!(r.reason);
        if let (Value::Object(o), Value::Object(f)) = (&mut ev, &r.fields) {
            o.extend(f.clone());
        }
    }
    ev
}

/// Whether a plain http address stays on this phone or the home network:
/// localhost and IP literals in 127/8, ::1, 10/8, 172.16/12, 192.168/16 and
/// fc00::/7. A name (server.lan) counts as remote: what it resolves to is
/// not checked here.
fn local_host(address: &url::Url) -> bool {
    match address.host() {
        Some(url::Host::Domain(d)) => d.eq_ignore_ascii_case("localhost"),
        Some(url::Host::Ipv4(ip)) => local_ip(IpAddr::V4(ip)),
        Some(url::Host::Ipv6(ip)) => local_ip(IpAddr::V6(ip)),
        None => false,
    }
}

fn local_ip(ip: IpAddr) -> bool {
    match ip {
        IpAddr::V4(v4) => v4.is_loopback() || v4.is_private(),
        IpAddr::V6(v6) => v6.is_loopback() || (v6.segments()[0] & 0xfe00) == 0xfc00,
    }
}

/// The host of a homeserver reached over plain http away from the phone
/// and the home network (audit NET-3), None when it is fine.
pub(crate) fn insecure_http(homeserver: &url::Url) -> Option<String> {
    if homeserver.scheme() != "http" || local_host(homeserver) {
        return None;
    }
    Some(homeserver.host_str().unwrap_or("").to_owned())
}

/// The refusal for such a server: for every login, whether the address was
/// typed, came from .well-known or from a QR code. A saved session is not
/// asked (restore keeps working).
pub(crate) fn refuse_insecure(homeserver: &url::Url) -> anyhow::Result<()> {
    match insecure_http(homeserver) {
        None => Ok(()),
        Some(host) => Err(Refusal {
            reason: "insecure_http",
            fields: json!({"server": host}),
            message: format!("{host} answers only over plain http; refused"),
        }
        .into()),
    }
}

/// Whether the consent page a new device sent is the account service's
/// own (audit NET-2): https, on the host of the service's issuer or of its
/// device authorization endpoint. None when it is; else the page's host.
pub(crate) fn foreign_consent_page(page: &url::Url, issuer: &url::Url, device_endpoint: Option<&url::Url>) -> Option<String> {
    let host = page.host_str().unwrap_or("").to_ascii_lowercase();
    let ours = |u: &url::Url| u.scheme() == "https" && u.host_str().is_some_and(|h| h.eq_ignore_ascii_case(&host));
    if page.scheme() == "https" && !host.is_empty() && (ours(issuer) || device_endpoint.is_some_and(ours)) {
        return None;
    }
    Some(host)
}

/// The account a browser login ended in against the one typed (audit
/// NET-7): only a full Matrix ID was typed is anything compared. None when
/// they agree (or nothing to compare); else the refusal.
pub(crate) fn other_account(typed: Option<&str>, got: &str) -> Option<Refusal> {
    let typed = typed.map(|u| u.trim()).filter(|u| u.starts_with('@') && u.contains(':'))?;
    if typed.eq_ignore_ascii_case(got) {
        return None;
    }
    Some(Refusal {
        reason: "other_account",
        fields: json!({"got": got, "wanted": typed}),
        message: format!("the browser logged in {got}, not {typed}; logged out again"),
    })
}

/// How the server refused the token refresh that ended a session.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Refused {
    /// the session has no refresh token: the request's own 401 is the answer
    NoRefreshToken,
    /// the homeserver's /refresh said M_UNKNOWN_TOKEN: it knows no such
    /// refresh token (Synapse: "refresh token does not exist")
    UnknownToken,
    /// any other refusal: M_FORBIDDEN (Synapse: rotated and the next one
    /// used, or expired), OAuth invalid_grant, anything else
    Other,
}

/// Whether a session the server ended is gone for good (a hard logout: the
/// store and media go, audit SEC6) or may come back with a login (kept, as
/// 3aab50e made it). No refusal of a token refresh ever counts: hard only
/// when every one of these holds:
/// - the session has no refresh token, so the request's own 401 is the
///   server's whole answer, and it said soft_logout false. A refused
///   refresh proves nothing: Synapse deletes a refresh token two rotations
///   back, so a session.json that missed two saved rotations is refused
///   with M_UNKNOWN_TOKEN ("refresh token does not exist") just as a
///   removed device is, and M_FORBIDDEN is a rotation already used;
/// - the session is a classic Matrix login on a server without an account
///   service (Synapse's own tokens): with one (MAS: matrix.org, tchncs.de)
///   an access token that merely ran out is answered "Token is not active"
///   with soft_logout false, the same as a session ended elsewhere (the
///   tchncs.de loss 3aab50e fixed), so nothing there tells the two apart.
/// A refresh that got no answer never reaches this (session_gone).
pub(crate) fn hard_logout(soft_logout: bool, account_service: Option<bool>, oauth_session: bool, refused: Refused) -> bool {
    !soft_logout && !oauth_session && account_service == Some(false) && refused == Refused::NoRefreshToken
}

#[cfg(test)]
mod tests {
    use super::*;

    fn url(s: &str) -> url::Url {
        url::Url::parse(s).unwrap()
    }

    /// Plain http only on the phone and the home network.
    /// Planted-bug: `local_host` true for every Domain (server.lan passes).
    #[test]
    fn plain_http_only_at_home() {
        for ok in ["https://matrix.org/", "http://localhost:8008/", "http://127.0.0.1:8008/", "http://127.5.0.1/",
                   "http://[::1]:8008/", "http://10.1.2.3/", "http://172.16.0.1/", "http://172.31.255.255/",
                   "http://192.168.2.15:8008/", "http://[fd00::1]/", "http://[fc00::1]/"] {
            assert_eq!(insecure_http(&url(ok)), None, "{ok}");
        }
        for (bad, host) in [("http://matrix.example.org/", "matrix.example.org"), ("http://server.lan:8008/", "server.lan"),
                            ("http://172.32.0.1/", "172.32.0.1"), ("http://8.8.8.8/", "8.8.8.8"),
                            ("http://[2001:db8::1]/", "[2001:db8::1]"), ("http://192.169.0.1/", "192.169.0.1"),
                            ("http://localhost.example.org/", "localhost.example.org")] {
            assert_eq!(insecure_http(&url(bad)).as_deref(), Some(host), "{bad}");
        }
        let err = refuse_insecure(&url("http://matrix.example.org/")).unwrap_err();
        let ev = with_refusal(json!({"ev": "login_error", "message": "x"}), &err);
        assert_eq!(ev["reason"], "insecure_http");
        assert_eq!(ev["server"], "matrix.example.org");
        assert_eq!(ev["message"], "x");
        // through context, as the login paths add it
        let wrapped = Err::<(), _>(err).map_err(|e| e.context("login")).unwrap_err();
        assert_eq!(with_refusal(json!({}), &wrapped)["reason"], "insecure_http");
        // any other error adds nothing
        assert_eq!(with_refusal(json!({"cmd": "x"}), &anyhow::anyhow!("other")), json!({"cmd": "x"}));
    }

    /// The consent page only on the account service's host, over https.
    /// Planted-bug: compare the scheme only (any https host passes).
    #[test]
    fn consent_page_only_on_the_account_service() {
        let issuer = url("https://account.matrix.org/");
        let device = url("https://account.matrix.org/oauth2/device");
        assert_eq!(foreign_consent_page(&url("https://account.matrix.org/link?code=AB"), &issuer, Some(&device)), None);
        assert_eq!(foreign_consent_page(&url("https://ACCOUNT.matrix.org/link"), &issuer, None), None);
        // the device endpoint's host alone is enough
        let other_issuer = url("https://matrix.example.org/");
        assert_eq!(foreign_consent_page(&url("https://auth.example.org/link"), &other_issuer, Some(&url("https://auth.example.org/oauth2/device"))), None);
        assert_eq!(foreign_consent_page(&url("https://account.matrix.org.evil.example/link"), &issuer, Some(&device)).as_deref(),
                   Some("account.matrix.org.evil.example"));
        assert_eq!(foreign_consent_page(&url("http://account.matrix.org/link"), &issuer, Some(&device)).as_deref(),
                   Some("account.matrix.org"));
        assert_eq!(foreign_consent_page(&url("https://example.invalid/device"), &issuer, Some(&device)).as_deref(),
                   Some("example.invalid"));
        // an http issuer (an http homeserver's) makes no page ours
        assert!(foreign_consent_page(&url("http://10.0.0.2/link"), &url("http://10.0.0.2/"), None).is_some());
    }

    /// A browser login as someone else than typed is refused; nothing to
    /// compare without a full Matrix ID.
    /// Planted-bug: compare the localparts only.
    #[test]
    fn browser_login_must_be_the_typed_account() {
        assert!(other_account(Some("@anna:example.org"), "@anna:example.org").is_none());
        assert!(other_account(Some(" @Anna:Example.org "), "@anna:example.org").is_none());
        assert!(other_account(None, "@eve:example.org").is_none());
        assert!(other_account(Some(""), "@eve:example.org").is_none());
        assert!(other_account(Some("anna"), "@eve:example.org").is_none());
        let r = other_account(Some("@anna:example.org"), "@eve:example.org").unwrap();
        assert_eq!(r.reason, "other_account");
        assert_eq!(r.fields, json!({"got": "@eve:example.org", "wanted": "@anna:example.org"}));
        assert!(other_account(Some("@anna:example.org"), "@anna:evil.example").is_some());
    }

    /// Which ended sessions wipe. Planted-bug: `hard = !soft_logout`
    /// (the tchncs.de case, an account service, would wipe again).
    #[test]
    fn only_a_session_gone_for_good_wipes() {
        use Refused::*;
        // classic Synapse without refresh tokens: the device removed elsewhere
        assert!(hard_logout(false, Some(false), false, NoRefreshToken));
        // the server's own hint that a login brings it back
        assert!(!hard_logout(true, Some(false), false, NoRefreshToken));
        // a refused refresh never counts: "does not exist" is also a
        // session.json two rotations behind, M_FORBIDDEN a rotation used
        assert!(!hard_logout(false, Some(false), false, UnknownToken));
        assert!(!hard_logout(true, Some(false), false, UnknownToken));
        assert!(!hard_logout(false, Some(false), false, Other));
        // an account service (MAS): an expiry and a removal look the same
        assert!(!hard_logout(false, Some(true), false, UnknownToken));
        assert!(!hard_logout(false, Some(true), false, NoRefreshToken));
        assert!(!hard_logout(false, Some(true), true, Other));
        // not known whether there is one (no answer to the question)
        assert!(!hard_logout(false, None, false, UnknownToken));
        // an OAuth session on a server said to have none
        assert!(!hard_logout(false, Some(false), true, UnknownToken));
    }
}
