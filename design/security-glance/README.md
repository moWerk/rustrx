# Rustrx Account glance: a standalone QML mockup

Rustrx is a native Matrix client for Sailfish OS (Qt 5.6, QML, Sailfish Silica). Its Settings open on an Account tab whose top part is a glance at how well the user's messages are protected. This folder (`design/security-glance/` in the Rustrx repository) holds that part on its own, runnable on a desktop, so a redesign can be written as QML that goes back into the app as it is. The app's own files are `qml/pages/settings/AccountTab.qml` (the whole tab), `qml/components/SecurityRing.qml` and `qml/components/LinkRow.qml`; this folder's copies differ from them only where the desktop needs it (icons, mock data, plain English texts). Nothing here is built into the app.

The brief (what the glance has to say, what the author thinks of it now, the constraints) is in `DESIGN-BRIEF.md`. Please read it first.

## Run it

```
qmlscene -I . main.qml                      # Qt 5
qml -I . main.qml                           # Qt 6 works too
```

The window shows the phone screen (480 x 1067, the Jolla Phone 2026 at pixel ratio 1.0; its real screen is 720 x 1600 at 1.5). Controls beside it switch the safeguards' states (all in place, backup off with two unverified devices, most missing, still checking), the ambience (dark or light, four highlight colours, the author's is Ruska #fe9a80), the pixel ratio, the font scale (the author's phone uses the "huge" font size), and two of the notes.

To render without a window:

```
QT_QUICK_BACKEND=software QT_QPA_PLATFORM=offscreen qmlscene -I . main.qml --shot=good.png --state=good
```

`--state` is `good`, `gaps`, `bad` or `checking`; `--light` for a light ambience.

## The files

- `AccountGlance.qml`: the tab, as it is in Rustrx 0.4.9, fed from `MockAccount.qml`. **The part to redesign** is marked: from the verdict line to the end of the notes. The avatar, name and debug log section around it are context.
- `SecurityRing.qml`: the ring of four arcs, a lock in the middle, the arcs' numbers.
- `LinkRow.qml`: a line with an arrow that leads somewhere (gaps, notes, Encryption, Devices).
- `PageButton.qml`: a Silica button with room around it.
- `MockAccount.qml`: the states the core reports, as plain properties.
- `ThemeIcon.qml`: the theme icons by name, drawn here because the Sailfish theme is not on a desktop. In Rustrx a `ThemeIcon` is `Image { source: "image://theme/" + name + "?" + color }`.
- `Sailfish/Silica/`: stand-ins for the few Silica types used (Theme, Label, BackgroundItem, SilicaFlickable, SectionHeader, TextSwitch, Button, ProgressCircleBase), close to Silica in size and colour. They exist so the files above can `import Sailfish.Silica 1.0` exactly as Rustrx does. Not to be redesigned.
- `Rustrx/Tunables.qml`: Rustrx's tuned numbers used here (the green, the ring's width).
- `main.qml`, `Choice.qml`: the window and its controls, not part of the design.

## For a redesign

- Keep the imports and the types Rustrx has: QtQuick 2.6, `Sailfish.Silica 1.0` (only the types in `Sailfish/Silica/qmldir`, plus Silica's real ones if the stand-in is extended to match), `Rustrx 1.0`. Qt 5.6: no Qt Quick Shapes, no `component` inline types, no `required` properties. Canvas, ShaderEffect, Rectangle, Silica's ProgressCircleBase are fine.
- Use `Theme.*` for every size, spacing, font size and colour (they follow the user's ambience and screen), `Tunables.securityComplete` for the one green.
- A new component goes in its own file next to the others.
- Text stays plain English here; Rustrx adds the translation ids when it takes the code over.
- Several distinct directions are welcome, each as its own copy of `AccountGlance.qml` (and of the ring, if it changes), so they can be compared side by side with the same controls.
