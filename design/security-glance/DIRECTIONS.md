# Four directions for the security glance

Each is its own copy of the tab, runnable in the mockup next to the 0.4.9 look:

```
qmlscene -I . main.qml                       # "Direction" in the controls, or "All side by side"
qmlscene -I . main.qml --design=all --state=gaps --shot=out.png
```

`--design` is `rail`, `halo`, `horizon`, `orbits`, `0.4.9` or `all`; `--huge` sets the large font scale, `--highlight=#87c7ff` another ambience colour. `renders/directions-*.png` show all five side by side (0.4.9 first) in each state.

## What all four share

- **A safeguard and its gap are one line** (`SafeguardRow.qml`). In 0.4.9 a missing safeguard is said twice: red in the legend, then again as a link below. Here the legend line itself becomes the link: its name, what the gap means, how to close it, the arrow to the right. In place, it is one short line of plain text.
- **Colour follows Sailfish's rule that colour says "pressable"**: a safeguard in place is static text in the highlight colour, a missing one is a link in the primary colour that lights up when pressed. Red is kept for the mark and the picture; green only when all are in place.
- **State is told by form before colour**: lit against unlit, closed against open, joined against broken. Ruska's highlight colour is close to the theme's red, so hue alone is not enough.
- **No numbers.** Each direction pairs a line with its part of the picture in its own way (below). Pressing a missing safeguard's line lights its part of the picture.
- The safeguards come from one list (`tab.safeguards`); a fifth is one more entry.
- The texts are the ones 0.4.9 has, unchanged. The safeguards keep their fixed order, so a gap stays where its safeguard is, instead of the "order to fix it" the old gap list had.

## Rail (`AccountGlanceRail.qml`, `RailMark.qml`)

No separate picture: the list is the visual. A thin line runs down it through one node per safeguard. Between two safeguards in place the line is lit; a missing one is an open red ring that the line stops short of, so a gap in the protection is a gap in the line. With everything in place it is one unbroken green line.

Why it fits: this is the most Silica of the four. A left-aligned list on the page margin, hairlines, nothing that is not text or a line, and it uses the least height, which matters with the huge font size. Pairing is free, since the mark is in the line itself.

## Halo (`AccountGlanceHalo.qml`, `HaloRing.qml`)

The ring stays, redrawn as glass and light: a hairline circle with the lock, and each arc in place lit with a soft light around it. A missing arc stays an unlit red hairline. Each line of the list is led by a small copy of the ring with only its own arc drawn, like a map's key.

Why it fits: Jolla's own metaphor for the look is glass and shining light, and Silica's circles are thin. It is the smallest step from 0.4.9: still ProgressCircleBase, the store's arc still twice as long, the lock and the warning sign as they are.

## Horizon (`AccountGlanceHorizon.qml`, `HorizonLine.qml`)

One line of light from edge to edge, the way a pulley menu's edge glows: a stretch per safeguard, as long as it weighs, with light rising from it when in place. A missing stretch is a red dotted trace with no light. Under each stretch sits its safeguard's icon, and the same icon leads its line in the list.

Why it fits: it borrows a thing Sailfish users already read without thinking (the glowing edge), runs flush with the screen edges as Sailfish graphics do, and is built from rectangles and gradients only. The icons give each safeguard a face. It needs three theme icons the app does not use yet: `icon-m-device`, `icon-m-backup`, `icon-m-computer` (names from the Sailfish icon reference; check them on the phone).

## Orbits (`AccountGlanceOrbits.qml`, `OrbitRings.qml`)

A closed circle per safeguard around the lock, the phone's store outermost and heaviest, since everything else lies inside what the phone stores. In place a circle is closed and lit; missing it is red and open, a way in. While checking, a short arc goes round each circle, as Silica's busy indicator does. Each line is led by the small circles with its own drawn.

Why it fits: the most striking of the four, and still only thin Silica circles in the ambience's colours. Closed against open needs no legend, and the all-good state is a seal. Another safeguard is another circle.

## Taking one into Rustrx

- Copy `SafeguardRow.qml` and the direction's component to `qml/components/`, and the glance part of its `AccountGlance*.qml` (between the two `----` marks, plus `safeguards`, `count`, `complete`, `checking`, `touched`) into `AccountTab.qml`.
- `ThemeIcon { name; color; size }` becomes `Image { source: "image://theme/" + name + "?" + color; sourceSize... }`, as in the app's SecurityRing and LinkRow.
- The breathing and circling of the "Checking…" state run only while that state is shown.
- Not tried here: Qt 6, landscape, a phone. Rendered with Qt 5.15's software renderer, where rotated arcs look rougher than Silica's own circles will.
