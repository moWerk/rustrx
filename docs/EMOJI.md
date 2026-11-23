# Emoji, custom emoji and stickers

The design is dodoradio's (2026-11-15):

- in the push-up menu, in place of the keyboard when open;
- five favourites on top;
- while typing ("Insert emoji"): emoji and custom Matrix emoji, inserted
  at the cursor;
- with an empty field ("Stickers and emoji"): emoji, custom emoji and
  stickers; an emoji goes into the field, a sticker is sent; an image
  that is both emoji and sticker is sent as a sticker (to use it as an
  emoji, type something first);
- the same picker for reactions.

## Part 1, done (0.4.4)

Unicode emoji (`qml/emoji-data.js`, Emoji 17.0, what Sailfish OS 5.2's
Noto Color Emoji draws), the panel (`qml/components/EmojiPanel.qml`),
"Insert emoji" in the push-up menu, insertion at the cursor, the five
most used (counted in `/apps/harbour-rustrx/emojiCounts`) shared with a
message's reactions, "more" in the reactions opens the picker.

## Showing custom emoji and text reactions (0.4.4)

Found by dodoradio: custom emoji in messages showed nothing, the raw
HTML had an `<img>` with an `mxc://` address that Qt cannot load.

- `TimelineModel` rewrites a message's HTML for `Text.StyledText`: an
  `<img>` with an `mxc://` address becomes the cached picture at a little
  more than the text's height (`emoteSize`), its name (`alt`) until it is
  there; any other `<img>` becomes its name, so a message never makes the
  phone load from an address (the spec allows only `mxc://`). The quoted
  reply fallback (`<mx-reply>`) goes.
- The core's `fetch_emote` caches each picture once, as a 96 px
  thumbnail, in `media/emotes/` (emptied with the media cache).
- A reaction with a custom emoji (its key an `mxc://` address, MSC4027)
  shows the picture; long text reactions are cut at 24 characters.
- Text reactions: "React with text" at the top of the picker when
  reacting (as in SchildiChat), the composer then says "Reacting to"
  and Send reacts with the text; `/react <text>` while replying does the
  same (as in nheko).

## Part 2: custom emoji and stickers

Status (0.4.4, built 2026-11-15, not yet tested): use and grab are in;
making new ones from a picture comes later. The core reads the packs
(`image_packs`: the user's own and the list of enabled ones from the
account data in the store, the enabled ones and the room's from the
server, kept for 24 hours and asked again when a pack's state event
comes through the sync; see "Where the packs come from" below), the picker shows a tab per pack, a sticker is sent with
`send_sticker`, `:shortcode:` in a message becomes the picture in its
HTML (`send_text` with `emotes`), a reaction with a custom emoji has its
mxc address as the key, and "Add :code: to my emoji" in a message's menu
writes the address into the user's own pack (no upload).

Matrix has them as image packs (MSC2545, what Element, Cinny, FluffyChat
and nheko use):

- `im.ponies.room_emotes` state events in a room (state key = pack),
  `im.ponies.user_emotes` in the account data (the user's own pack),
  `im.ponies.emote_rooms` in the account data (other rooms' packs the
  user enabled everywhere).
- Each image: a shortcode, an `mxc://` URL, `body`, `info`, and `usage`
  `["emoticon"]`, `["sticker"]` or both (a pack's `usage` is the
  default).

What the core needs:

1. `image_packs {room_id}`: the user's pack, the enabled rooms' packs
   and the room's own, merged; images fetched as thumbnails into the
   media folder like avatars; answered by an `image_packs` event.
2. Sending a sticker: an `m.sticker` event (`body`, `url`, `info`)
   through the room's timeline, so it shows at once like other sends.
3. Custom emoji in a message: the body keeps `:shortcode:`, the
   formatted body gets `<img data-mx-emoticon src="mxc://…" alt=":code:"
   title=":code:" height="32">`. The core sends markdown only today, so
   this needs an HTML send path: the composer keeps the shortcode, the
   core turns known shortcodes into the image tag when it builds the
   formatted body.
4. A custom emoji as a reaction: the key is the `mxc://` URL with
   `shortcode` in the content (MSC4027); the timeline shows such
   reactions as images.

UI: the picker gets a tab per pack after the Unicode groups; with an
empty field and a pack image whose usage includes "sticker", a tap sends
it; the menu entry says "Stickers and emoji" then.

Gaps:
- a `/me` message (m.emote) carries no HTML into the timeline yet, so a
  custom emoji there still shows as its shortcode;
- an edit sends the body only, so a custom emoji in an edited message
  loses its picture;
- with sliding sync a room's own packs still need its whole state from
  the server once per room and day (see below);
- a custom emoji reaction carries no shortcode into the timeline, so
  "Add to my emoji" names it after its picture's id.

## Where the packs come from (0.8.2)

- The classic /sync puts a room's whole state into the SDK's store and
  keeps it current, pack events included: with it (and a room the store
  marks fully synced) the room's own packs and an enabled pack of a
  joined room are read from the store, no request.
- Sliding sync brings only the state types it asks for. The SDK's room
  list service (matrix-sdk-ui 0.19.1) asks a fixed list
  (`DEFAULT_REQUIRED_STATE`, plus pinned events for a subscribed room)
  with no way to add `im.ponies.room_emotes`, and the state API has no
  type filter; the only way to find the state keys of a room's packs is
  `GET /rooms/{id}/state` (AsteroidOS: 2053 events, about 0.9 MB, 0.4 to
  8.4 s). An enabled pack is one event, asked by its state key.
- So the answers are kept, in memory and in the account's SDK store
  (custom values `rustrx.packs.v1.room:<room id>`,
  `rustrx.packs.v1.global` and `rustrx.packs.v1.missing`: encrypted with the store, gone with it at
  logout, per account), read lazily per room. A room's kept packs are
  shown at once; older than 24 hours they are asked again behind that,
  and a changed answer goes out as a new `image_packs`. An enabled pack
  older than 24 hours is asked again before answering (one small event).
- Invalidation: a room's `im.ponies.room_emotes` state event from the
  sync (the timeline of a synced room, or the classic sync's state)
  drops that room's packs and its enabled packs, in memory and in the
  store; an answer asked before an invalidation is not kept (`epoch`,
  with the store writes under one lock). The own pack and the enabled
  list come from the account data in the store; what the server
  answered for them when the store had none is kept for the login only
  and dropped when the sync brings that type; its "none" (the usual
  answer, a 404) is kept in the store too (`rustrx.packs.v1.missing`,
  24 hours like the packs), so a restart asks nothing, and is dropped
  when the sync brings that type or "Add to my emoji" writes the own
  pack. The store's account data is read first, so a kept "none" never
  hides one the sync brought. An enabled pack the server
  does not have (404, or 403) is kept as gone like any answer. A pack
  changed in a room whose timeline this phone did not see (a gap after
  long offline, a room left and joined again) shows within 24 hours.

