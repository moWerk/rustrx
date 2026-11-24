// SPDX-License-Identifier: GPL-3.0-or-later
//! One redaction step for every log line the core hands on (LogWriter):
//! what servers and serde can echo stays out of the debug log, what a log
//! is read for stays in (levels, targets, status codes, errcodes, room,
//! event and user ids, command names).
//!
//! - a URL's query and fragment
//! - the value after `access_token`, `refresh_token`, `token=`, `Bearer`,
//!   and a UIAA `session`, whatever its length
//! - a server's error body that is not the standard errcode and message
//!   (ruma's `Json(..)` and `NotJson { .. }`); the message cut to
//!   MAX_SERVER_MESSAGE characters
//! - serde's echo of a string value (`invalid type: string "..."`) and the
//!   value of a JSON `"body"` or `"formatted_body"`

const REDACTED: &str = "[redacted]";

/// A server's error message is kept to this many characters.
const MAX_SERVER_MESSAGE: usize = 120;

/// Keys whose value is a secret, matched without case; the value is the
/// run of token characters after them.
const SECRET_KEYS: [&str; 5] = ["access_token", "refresh_token", "token=", "bearer ", "session: some("];

/// Prefixes of a quoted text whose content goes.
const QUOTED_CONTENT: [&str; 6] = [
    "string \"",
    "\"body\":\"",
    "\"body\": \"",
    "\"formatted_body\":\"",
    "\"formatted_body\": \"",
    "bytes: b\"",
];

pub(crate) fn redact(line: &str) -> String {
    let line = cut_queries(line);
    let line = cut_error_bodies(&line);
    let line = mask_secrets(&line);
    let line = mask_quoted(&line);
    cap_messages(&line)
}

fn is_token_char(c: char) -> bool {
    c.is_ascii_alphanumeric() || "-._~+/=%".contains(c)
}

/// The end of a quoted text whose opening quote is just before `from`
/// (Rust's Debug and JSON escape a quote with a backslash): the index of
/// the closing quote, or the line's end.
fn quoted_end(s: &str, from: usize) -> usize {
    let mut escaped = false;
    for (i, c) in s[from..].char_indices() {
        match c {
            _ if escaped => escaped = false,
            '\\' => escaped = true,
            '"' => return from + i,
            _ => {}
        }
    }
    s.len()
}

/// `http(s)://host/path?query#fragment` keeps `http(s)://host/path`.
fn cut_queries(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut rest = s;
    while let Some(start) = ["https://", "http://"].iter().filter_map(|p| rest.find(p)).min() {
        let mut end = rest[start..]
            .find(|c: char| c.is_whitespace() || "\"'<>)]".contains(c))
            .map_or(rest.len(), |e| start + e);
        out.push_str(&rest[..start]);
        match rest[start..end].find(['?', '#']).map(|q| start + q + 1) {
            // cut already: a line redacted twice stays the same
            Some(q) if rest[q..].starts_with(REDACTED) => {
                end = q + REDACTED.len();
                out.push_str(&rest[start..end]);
            }
            Some(q) => {
                out.push_str(&rest[start..q]);
                out.push_str(REDACTED);
            }
            None => out.push_str(&rest[start..end]),
        }
        rest = &rest[end..];
    }
    out.push_str(rest);
    out
}

/// The index just past the bracket that closes the one at `open`, quoted
/// text skipped; the line's end when it never closes.
fn closing(s: &str, open: usize) -> usize {
    let mut depth = 0usize;
    let mut i = open;
    while i < s.len() {
        let c = s[i..].chars().next().unwrap_or(' ');
        match c {
            '"' => {
                i = quoted_end(s, i + 1);
            }
            '(' | '{' | '[' => depth += 1,
            ')' | '}' | ']' => {
                depth = depth.saturating_sub(1);
                if depth == 0 {
                    return i + 1;
                }
            }
            _ => {}
        }
        i += c.len_utf8();
    }
    s.len()
}

/// ruma's error bodies other than the standard one: `body: Json(..)`,
/// `body: NotJson { .. }`.
fn cut_error_bodies(s: &str) -> String {
    let mut out = s.to_owned();
    for marker in ["body: Json(", "body: NotJson {"] {
        let mut from = 0;
        while let Some(at) = out[from..].find(marker).map(|i| from + i) {
            let open = at + marker.len() - 1;
            let end = closing(&out, open);
            let kept = format!("body: {REDACTED}");
            out.replace_range(at..end, &kept);
            from = at + kept.len();
        }
    }
    out
}

fn mask_secrets(s: &str) -> String {
    let lower = s.to_ascii_lowercase();
    let mut out = String::with_capacity(s.len());
    let mut last = 0;
    let mut at = 0;
    while at < s.len() {
        let Some((key_at, key)) = SECRET_KEYS.iter().filter_map(|k| lower[at..].find(k).map(|i| (at + i, *k))).min() else {
            break;
        };
        // past separators and quotes to the value
        let mut v = key_at + key.len();
        while v < s.len() && s[v..].starts_with([' ', ':', '=', '"', '\'', '(']) {
            v += 1;
        }
        if lower[v..].starts_with("some(") {
            v += "some(".len();
            while v < s.len() && s[v..].starts_with(['"', '\'']) {
                v += 1;
            }
        }
        let end = s[v..].find(|c: char| !is_token_char(c)).map_or(s.len(), |e| v + e);
        if end > v {
            out.push_str(&s[last..v]);
            out.push_str(REDACTED);
            last = end;
        }
        at = end.max(key_at + key.len());
    }
    out.push_str(&s[last..]);
    out
}

fn mask_quoted(s: &str) -> String {
    let mut out = s.to_owned();
    for prefix in QUOTED_CONTENT {
        let mut from = 0;
        while let Some(at) = out[from..].find(prefix).map(|i| from + i) {
            let start = at + prefix.len();
            let end = quoted_end(&out, start);
            out.replace_range(start..end, REDACTED);
            from = start + REDACTED.len();
        }
    }
    out
}

/// A server's `message: "..."` cut to MAX_SERVER_MESSAGE characters.
fn cap_messages(s: &str) -> String {
    let mut out = s.to_owned();
    let mut from = 0;
    while let Some(at) = out[from..].find("message: \"").map(|i| from + i) {
        let start = at + "message: \"".len();
        let end = quoted_end(&out, start);
        from = match out[start..end].char_indices().nth(MAX_SERVER_MESSAGE) {
            Some((cut, _)) => {
                out.replace_range(start + cut..end, "…");
                start + cut + "…".len()
            }
            None => end,
        };
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    /// None of the probes reaches the log line; what a log is read for
    /// stays. Planted-bugs: return the line unchanged from `redact`; leave
    /// `bearer ` out of SECRET_KEYS (the Bearer probe stays).
    #[test]
    fn secrets_and_content_go_ids_and_codes_stay() {
        let cases: [(&str, &[&str], &[&str]); 9] = [
            (
                "ERROR matrix_sdk::http_client: Error while sending request: Reqwest(reqwest::Error { kind: Request, url: \"https://matrix.example.org/_matrix/client/v3/sync?since=s123_PROBE&timeout=30000\", source: hyper::Error(Connect) })",
                &["PROBE", "since=", "timeout="],
                &["https://matrix.example.org/_matrix/client/v3/sync?", "kind: Request", "Connect"],
            ),
            (
                "WARN rustrx: login: https://account.example.org/authorize?client_id=X&state=PROBE#frag",
                &["PROBE", "client_id", "frag"],
                &["https://account.example.org/authorize?"],
            ),
            ("INFO x: access_token=abcPROBE24charsxxxxxxxx next", &["PROBE"], &["access_token=", "next"]),
            ("INFO x: {\"access_token\":\"shortPROBE\",\"user_id\":\"@a:b.org\"}", &["PROBE"], &["@a:b.org"]),
            ("DEBUG x: Authorization: Bearer syt_PROBE_tok, room !r:b.org", &["PROBE"], &["Bearer", "!r:b.org"]),
            (
                "ERROR matrix_sdk::http_client: Error while sending request: Api(Server(MatrixError(Error { status_code: 400, body: Json(Object {\"errcode\": String(\"M_X\"), \"secret\": String(\"PROBE (with) } brackets\")}) })))",
                &["PROBE", "secret"],
                &["status_code: 400", "body: [redacted]"],
            ),
            (
                "ERROR matrix_sdk::http_client: Error while sending request: Api(Server(MatrixError(Error { status_code: 502, body: NotJson { bytes: b\"<html>PROBE\\\"</html>\", deserialization_error: Error(\"x\") } })))",
                &["PROBE", "html"],
                &["status_code: 502", "body: [redacted] })))"],
            ),
            (
                "WARN rustrx::core: cmd=send_text invalid type: string \"PROBE \\\"quoted\\\" text\", expected a boolean at line 1 column 40",
                &["PROBE", "quoted"],
                &["cmd=send_text", "invalid type: string", "expected a boolean", "column 40"],
            ),
            (
                "WARN x: event {\"type\":\"m.room.message\",\"content\":{\"body\":\"PROBE one\",\"formatted_body\": \"<b>PROBE</b>\"},\"event_id\":\"$e:b.org\"}",
                &["PROBE"],
                &["m.room.message", "$e:b.org", "\"body\":\"[redacted]\""],
            ),
        ];
        for (line, gone, kept) in cases {
            let out = redact(line);
            for g in gone {
                assert!(!out.contains(g), "{g:?} left in {out}");
            }
            for k in kept {
                assert!(out.contains(k), "{k:?} lost from {out}");
            }
        }
        // the SDK's standard error: errcode and message stay, a long one is cut
        let standard = "ERROR matrix_sdk::http_client: Error while sending request: Api(Server(MatrixError(Error { status_code: 403, body: Standard(StandardErrorBody { kind: Forbidden, message: \"You are not invited to this room.\" }) })))";
        assert_eq!(redact(standard), standard);
        let long = format!("x: message: \"{}\" end", "ä".repeat(300));
        let out = redact(&long);
        assert!(out.contains(&format!("message: \"{}…\" end", "ä".repeat(MAX_SERVER_MESSAGE))), "{out}");
        // a UIAA session id
        let uiaa = "WARN x: Uiaa(UiaaInfo { flows: [], completed: [], params: None, session: Some(\"PROBEsession\"), auth_error: None })";
        assert!(!redact(uiaa).contains("PROBE"), "{}", redact(uiaa));
        // redacting twice changes nothing more
        for (line, _, _) in cases {
            assert_eq!(redact(&redact(line)), redact(line), "{line}");
        }
        // it runs where a panic would end the app (a command's log line):
        // no cut of a line, at any character, makes it panic
        let edges = ["message: \"äö", "string \"ä\\", "access_token", "Bearer ", "body: Json(Object {\"", "https://ä.example/x?ä#", "\"body\":\"\\"];
        for line in cases.iter().map(|c| c.0).chain(edges) {
            for (i, _) in line.char_indices().chain([(line.len(), ' ')]) {
                redact(&line[..i]);
                redact(&line[i..]);
            }
        }
        // ordinary lines pass as they are
        for plain in [
            "INFO rustrx::core::sync: sync: 12 rooms, 3 invites",
            "WARN rustrx::core: tokens refreshed",
            "WARN rustrx::core: cmd=open_room not logged in",
            "ERROR matrix_sdk::http_client: Error while sending request: Api(Server(MatrixError(Error { status_code: 401, body: Standard(StandardErrorBody { kind: UnknownToken(UnknownTokenErrorData { soft_logout: false }), message: \"Token is not active\" }) })))",
        ] {
            assert_eq!(redact(plain), plain);
        }
    }
}
