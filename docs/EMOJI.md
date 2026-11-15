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
(`image_packs`: the user's own from the account data, the enabled ones,
the room's from its state, fetched from the server each time a room
opens), the picker shows a tab per pack, a sticker is sent with
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

Gap: a `/me` message (m.emote) carries no HTML into the timeline yet, so a
custom emoji there still shows as its shortcode.
