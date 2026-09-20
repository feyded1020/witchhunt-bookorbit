# Release Notes

User-facing changes only. Full commit history is in git log.

## 2.31 — 2026-09-20

Everything since 2.30. Four larger reading sizes, a text size for the menus, and a firmware that came out smaller than 2.30 despite gaining both.

### Bigger text

- **Four new reading sizes: 20, 22, 24 and 26 pt**, available as the default size and as a per-book override. 18 pt was the ceiling, so a reader who cannot comfortably read it had nothing to move up to. 20 pt is a real typeface cut at that size — eight new faces, two families in four styles. 22, 24 and 26 pt enlarge the 20 pt master, because cutting all three for real would cost about 1.9 MB of flash the device does not have. The enlargement is area-weighted, and each distinct letter on a page is enlarged once and reused for the rest of that page.
- **Sizes are labelled by their point size** — 10pt through 26pt — instead of Tiny / Small / Medium / Large / X-Large. The adjectives ran out at "extra large", and they never said what they meant: "medium" was whatever 14 pt happened to be. Four places in the firmware kept their own copy of the list, two of which had gone stale, and one had them out of order — the picker read "12pt 14pt 16pt 18pt 10pt". The list is now derived from the single table that defines the ladder, so a size can only be added in one place. Your saved size is carried across, so every book keeps the size it was being read at.
- **Headings are sharper.** A heading is drawn by snapping its requested size to the nearest real face and enlarging whatever is left over. With the ladder stopping at 18 pt, a heading that wanted 25 pt took the 18 pt face and enlarged it by nearly half. There are rungs above it to land on now, so most headings are enlarged by a few percent or not at all.
- **A font installed on the card renders at every size too.** No `.cpfont` in existence ships anything above 18 pt, so the new sizes were unreachable on an installed font: the reader quietly put your typeface back to a built-in one. Your chosen typeface is now scaled from its closest face to the size you asked for, rather than being replaced.
- **Fix: changing the font or size from the reader menu did nothing until the book was closed and reopened** — and on a card font a size change did worse than nothing, silently swapping your typeface for the built-in one. Both symptoms, "nothing happens" and "it reverts to the default font", were the same bug seen from two sides: nothing re-loaded the font when an override changed, and the lookup that hands the font out accepts only an exact size match.
- Fix: a card font family that did not ship the size you asked for was read into memory in full and then not used.
- **New: Font Scaling Test** (Settings → System). Draws the whole ladder, 10 through 26 pt, for both built-in families, marking which sizes are real cuts and which are enlarged. It is here because the measurements cannot settle the question: they average out over a letter's whole area, and what a reader actually notices is edge definition and stem weight, which is exactly what enlarging trades away. So the decision rests on looking at it, on the panel, at its real resolution.

### The menus

- **New: UI Font Size** (Settings → Display: Normal / Large). Menu rows on a high-resolution panel are small and fiddly to hit with a finger. Large moves every menu, header and list row up one step. The reader's own status bar is deliberately left alone: it sets the text area a book is laid out into, and scaling it would re-paginate your whole library.
- Fix: in French, Brazilian Portuguese, Russian, Ukrainian and Belarusian, the setting that sets the size of the text *in a book* was labelled as the size of the text in the menus. It has always been the reader's size; the label was wrong, and with a real menu text size now sitting a tab away the two would have read alike.
- **Settings toggles are drawn as switches** rather than the words ON and OFF. Twenty-eight rows. Fix: Quick Resume on Timeout was the one on/off setting that kept showing text, because it was declared as a two-option list whose two options happened to be Off and On.
- **Every yes/no question has buttons on it now.** Clearing the cache, repairing the screen, confirming a firmware update and the general confirmation prompt all drew a centred question with nothing to press — the button-hint strip along the bottom was the only affordance, which on a device you are using with a finger is no affordance at all. The hint strip stays everywhere: it labels the physical keys, and on a device without a touchscreen it is the only affordance there is.
- **The reading-light pull-down is a drawer.** Swiping down from the top edge opened the Reading light settings screen — and opened it wrong: a screen titled "Reading light" whose single row was also "Reading light", costing a second tap to reach the controls. It is now a panel pulled over the page, only as tall as its contents, with the rest of the screen left standing.
- **Fix: a button press in a list could be missed entirely.** Moving the selection held the screen-drawing lock for the whole redraw, and buttons are read once per pass through the main loop — so a press that both started and ended inside that window was never seen at all. The longer the list, the slower the redraw, and the more presses went missing.
- Fix (touch): the first row of a list could sit hidden above a blank row until the next redraw, because the page size was worked out from a two-line row height where the list draws single-line rows. Seen in the reader menu's font size picker as soon as it grew to ten entries.
- **Fix: a partial repaint could draw on top of the previous screen.** Handing back the second framebuffer — which the home screen does while loading covers, and the reader does around building a chapter — threw away the frame that was actually on the panel, so anything that repainted part of the screen rather than all of it composited over a stale one.

### Speed

- **The font list is usable again.** Moving the cursor one row cost seconds: the preview re-read the font from the card, re-rendered the sample, and rewrote the whole 3.5 MB flash font cache — once per row. That last one also destroyed the selected font's cached copy, so merely opening the list made the next book you opened pay to rebuild it. Previews are now kept on the card and blitted back, about 120 ms a row with nothing allocated at all. On entry, any that are missing are built in one announced pass — "Creating font preview N/M", six families in around four seconds — and Back cancels it, after which the rest build as you browse.
- **Going in and out of a settings submenu no longer costs a two-second refresh each time.** The X3 clears accumulated ghosting with a slower refresh whenever a screen replaces something else, and settings asked for one on every return from a child screen, even when one had run three screens earlier and the panel was already clean. It is now spent only once several fast refreshes have gone by.
- Reads from the SD card transfer a sector in batches instead of one byte at a time — which is glyphs, chapter caches, cover images and the book itself.
- Trimming a label that is too long for its row is four to five times cheaper, and more than that the more there is to trim.

### Fixes

- **Fix: the web file browser could fail to reach Wi-Fi at all on an X3**, showing a plain connect timeout with nothing in the log to explain it. Joining a network is itself memory-hungry, and the radio was being brought up while a 52 KB framebuffer and the loaded font were still resident. Of the eight screens that use Wi-Fi, four freed that memory only after the join had succeeded, which is too late to help the join. The memory is now released at the point the radio comes up, which covers all of them.
- **Fix: a device left sitting in an OPDS catalogue never went to sleep**, and ran at full speed until the battery was flat. Browsing a catalogue, reading a book's details and looking at an error message are all screens you read, and they sleep like any other now; only the states with a transfer actually in flight keep the device awake.

### Room on the device

2.30 shipped with the firmware filling 96.7% of the space allotted to it — 219 KB spare on a 16 MB device. That margin is what a wireless update has to fit inside, and it was nearly gone. 2.31 adds an entire new typeface size and a second menu text size and still lands at 92.8%, with about 470 KB spare. Three changes paid for it, and none of them is visible while reading:

- **The other 23 languages ship compressed**, and whichever one you have selected is unpacked into a reserved slot in flash. Every language used to sit in the firmware in full, at all times, and a reader uses one. 232 KB.
- **Font data that describes the typeface rather than the size is stored once per family** instead of once per face — which characters it covers, the kerning classes, the ligatures. Across 55 faces that was 226 KB of byte-for-byte duplication.
- **Each letter's record is 6 bytes rather than 16.** Most of what was stored in it can be worked out from the letter's own width and height. About 480 KB, and looking a letter up is roughly 10% quicker as well, so this is not the trade it looks like.

### With thanks to

Where the code here is someone else's it is credited in the source file it lives in; where the idea is theirs and the code is ours, the source says that too.

- **Justin Mitchell** (@itsthisjustin) — the reading-light drawer's behaviour, and routing a held finger to a slider (crosspoint-reader #2983).
- **Jay Silverman** (@Techneaux) — the diagnosis behind lists dropping button presses (#3534).
- **Sung-jin Brian Hong** (@serialx) — giving a laid-out page's elements a single owner (#3518).
- **Uri Tauber** (@uritaube) — letting the OPDS catalogue sleep like any other screen (#3547).
- **Osakana Taro** (@osakanataro) — batching the SD card's SPI reads (#3501).
- The size ladder's idea — snap to a real face and scale only the remainder — is from **CidVonHighwind/microreader**.

### Upgrade notes

- **Chapters are re-indexed once per book the first time you open them**, because headings now reach the new larger faces and so break across lines differently. Your place in every book is kept.
- **Your reading size is carried over**, both the global one and any per-book override, even though the sizes were renumbered to put them in order.
- **If you read in a language other than English, the first boot after this update takes about a second longer** while that language is unpacked into flash. It happens once per update that changes any text, and not at all if none of the text changed. English is unaffected.
- **The first font load after this update is slower.** The flash font cache is rebuilt from the `.cpfont` files on your card, because the language slot now sits at the end of the same area of flash. Nothing you installed is lost — the flash copy was only ever a cache of what is on the card.
- The three new labels — UI Font Size, Font Scaling Test and the font-preview progress notice — are translated in German, French, Spanish, Italian, Dutch, Portuguese (PT and BR), Polish, Russian, Ukrainian, Belarusian, Slovenian and Swedish. Turkish and Vietnamese show the English text for those rows until a native speaker fills them in; the eight partly-translated languages are unchanged.

## 2.30 — 2026-09-16

Everything since 2.26. Two new devices, touch control throughout, and a long run of fixes to the boot and sleep paths.

### Two new devices

- **The Xteink X4 Pro and the LilyGo T5 S3 Pro now run this firmware**, alongside the X3 and X4. Both are ESP32-S3 devices with a touchscreen; the T5 S3 adds a backlight, and the X4 Pro a two-channel light whose warmth can be set separately. On both, the capacitive Home key is Confirm on a tap and Back on a hold. Touch is an addition, never a replacement: every button still does what it did, and all of it can be switched off.
- **Firmware is now published per device.** `firmware.bin` stays the combined X3/X4 binary under the name it has always had, so devices in the field keep updating as before; the new boards take `firmware-x4pro.bin` and `firmware-lilygo.bin`. The update check asks for its own device's file, and both the over-the-air path and the SD-card path now refuse an image built for a different device rather than installing it.
- Getting there meant unpicking an assumption that ran through the whole firmware: it asked "is this an X3?" and treated everything else as an X4. Pin assignments, the SD and display buses, the battery gauge, the clock chip, the panel controller and the display's own capabilities are now read from a per-device profile, so a fourth device is a profile rather than an edit in eighty places.
- Fix: the recovery firmware mode — hold Up and Power at boot — could not be reached on the X4 Pro or X4 Classic, because Up is the pin the chip samples at reset to decide whether to run the firmware at all. Holding it meant the firmware never started, which from outside is indistinguishable from a dead device: the documented escape from a bad SD card was the thing that looked broken. Those devices use Down instead.
- Fix (T5 S3): leaving the reader menu, and leaving a book, showed the old screen and the new one superimposed for a refresh. Fix (T5 S3): two cleaning passes in a row left both pages visible at once.
- Fix: on devices where the touch controller, the clock and the battery gauge share one wire, two of them could talk at once and corrupt each other — including a case where the display's power rails were raised while a frame was being clocked out.

### Touch

- **The whole interface answers to touch.** Rows, book covers, the home menu, the settings tabs, the on-screen keyboard, sliders, the dictionary's word picker, footnote markers and the button-hint strip along the bottom all respond to a finger. Lists use *point-then-confirm* — the first tap moves the highlight, a second tap on the same row opens it — so a mis-tap costs one more tap rather than an action to undo. If you would rather rows opened on the first tap, **Settings → Controls → Tap Action** switches it.
- **Twenty-odd gestures, each one assignable** like a button, under **Settings → Controls → Gesture actions**. Out of the box: tap the outer thirds to turn pages, tap the middle for the reader menu, hold left or right to skip a chapter, hold the middle to look a word up, hold the bottom to star the page, pinch to resize text, and turn two fingers to rotate the screen. Swipe sideways in a page-turn zone to jump ten pages. Swipe down from the top edge for the reading light, up from the bottom edge for the reader menu, and up or down at the left and right edges for brightness and warmth. **Settings → Controls → Gesture overview** draws the zones and your own assignments on the device itself, so it is always right even if the manual is not.
- **Hold the top-left corner to turn the reading light on and off, on every screen** — not just while reading. The corners answer only to a hold, never a tap, so no tap anywhere changed meaning; the other three are free for you to assign. Double-pressing Power does the same thing without touching the glass, which is the one that works in the dark.
- Two master switches, deliberately separate: **Touch Page Turn** governs the reading page (Off / Tap Zones / Swipe / Tap Zones Inverted) and **Touch Navigation** governs everything outside it. Turning off page turns so a resting thumb cannot flip a page should not also stop you tapping a book in the library. Neither can strand you — the reader menu stays reachable with page turns off, and Back and Confirm come in as button presses below the level these settings act on.
- The LilyGo T5 S3 has a Down key but no Up key, so paging a list *backward* has no button there: **tap the scroll bar** above the thumb, or swipe over the list. Both work on the X4 Pro too.
- Fix: taps were lost when the reader was busy or coming out of sleep, the carousel's side covers ignored taps, list rows were sized for an 800-pixel screen, and taps on the side button hints did nothing. Fix: a gesture that was refused said nothing at all, and "Built-in" did not say what it would actually do.
- On the X3 and X4 the touch code is compiled out entirely, so none of this costs those devices anything.

### Boot, sleep and wake

- **Fix: the device could hang on the way to sleep with the sleep screen showing and every button dead**, recoverable only with the reset pin (#155). The last thing the sleep path did was wait for the power button to be released, with no way out — a sticky switch, or a finger that never lifted, and it waited forever. It now gives up after five seconds and sleeps anyway.
- **Fix: an X4 could sit on the sleep screen ignoring the power button** (#155), which reads exactly like a dead device. Some X4 units do not latch their own power: they stay alive only while the button is physically held, and the firmware did not close that latch until well into the boot — after reading settings, checking the wake gate, three memory integrity walks and the update state. Anything shorter than all of that and the device dropped dead mid-boot with the sleep image still on the glass. The latch is now the first thing the firmware does.
- **Fix: one too-short press on the power button quietly powered the device off** instead of doing nothing. Refusing a press is a "nothing happened" answer, but on the X4 it was taking a path that cut the battery latch — downgrading the device from "asleep, wakes on a tap" to "off, needs a hold long enough to carry a whole boot", and losing the clock with it.
- **Fix (X3): the SD card stayed powered through deep sleep**, draining the battery, because the rail was only released on the X4 branch of the sleep path.
- **Fix: a deadlock while the reader was starting up**, plus a set of races and deadlocks between the drawing task and the main loop — including a shared file position used by two tasks at once, and the task watchdog being fed from a task that had never subscribed to it.
- **New Boot Diagnostics page** under Settings → System. It reports how this boot started, where the last sleep stopped, and pairs each sleep with the boot that followed it. The point is that "it failed to sleep" and "it failed to wake" look identical from outside, and almost nobody has a serial cable attached. One screenful, no scrolling — it is meant to be photographed into a bug report.
- Fix (X4 Pro): the main peripheral rail was dropped during deep sleep.

### The screen

- **Fix (X3): vertical banding on grayscale images.** The four-level image path staged its greys as a correction over a black-and-white base and resolved them with a short pulse — and that short pulse is what exposes the panel's own drive unevenness, showing as stripes every eight lines on anything dithered. Those images now carry the whole frame in one pass at the panel's full waveform. Measured upstream on the affected panel, the column variation drops from 3.5–4.7% of the black-to-white range to 0.9% and the eight-line pattern disappears. This covers the bitmap sleep screen and the book-cover sleep screen — a JPEG or PNG cover is decoded to a bitmap first, so it takes this path too — and grayscale images in the image viewer. Text rendering and the reader's own pages are untouched: the short pulse suits them.
- **Sleep images on the T5 S3 are drawn at the panel's real depth.** Every device dithered sleep images to four grey levels because that is all the display pipeline could carry, which is a hard ceiling on the older panels but not on this one. On a panel with more levels the picture is now decoded once and shown whole, and the "Equalize" filter is applied more strongly to match, since there is finally somewhere to put the extra range. On a bimodal cover that gains about six points of brightness and a fifth more local contrast.
- **New: Repair Screen** (Settings → System). Fast page refreshes deliberately skip the eraser, so ghosting builds up in a way the periodic clean-up does not fully clear. This drives every pixel hard between black and white several times and settles on white — about twenty seconds, nothing is deleted. It is a maintenance action, not a fix for ghosting while you read.
- **New: Screen Edge Margin** (Settings → Display: Narrow / Medium / Large). On the T5 S3 the case comes close enough to the live pixels that text at the edge is hard to read. This adds 0, 5 or 10 pixels on top of what the device declares. Narrow is the default and is exactly the old behaviour, so nothing moves unless you ask.
- **New: a "Lighter" step for text darkness**, in Settings and in a book's own overrides. Every existing step added weight and none took it off. It is modest — it moves the partly-inked edges of letters, not their cores.
- **Fix: an idle reader flashed the whole screen every fifteen minutes or so.** The ghosting budget is counted in pages, but the once-a-minute clock tick was spending it like a page turn, so the budget drained whether or not anyone was reading and a full refresh eventually fired for no reason but a changing digit.
- The "sunlight fading" toggle is now offered only on the devices whose glass actually fades, since enabling it costs panel time on every page.
- The clock in the status bar can sit at either end.

### Reading light

- The T5 S3's backlight is driven, on/off and brightness. On a two-channel light, warmth gets the same quick controls brightness has.
- **Brightness and warmth now preview live as you move the slider**, and Cancel puts back what you had. Choosing a light level used to mean picking a number, confirming, looking at the screen and going back in. If the light was off, previewing turns it on and then puts it back off — asking for a brightness is not asking for the light.
- Fix: brightness and warmth were not actually saved. Fix: one of the quiet background restarts could bring the light back at the wrong level.

### Reading

- **Fix: coming back from a footnote landed at the start of the chapter** instead of the page the note was on — and so did keeping your place across a font-size change. Positions are anchored to a paragraph so that re-flowing cannot move them, but the paragraph counter only saw paragraphs sitting directly inside the document body, and a large share of real books (anything Calibre has produced, for one) nest them one level deeper. Every page in such a chapter recorded "paragraph 0", which was then read as a real position meaning the top.
- **Fix: following a link could exit the book.** Back has only the saved position to return to, and a change intended to treat contents links as navigation rather than as a detour stopped saving one — so Back fell through and closed the book.
- **The footnote list now separates notes from navigation links.** A page's internal links were all recorded as footnotes, so a chapter's contents link sat among the real notes. Notes come first, then links, each marked and in page order. They stay in the list rather than being hidden, because without touch the list is the only way to reach a link at all.
- **Fix: hidden text was displayed.** Publishers use the HTML `hidden` attribute for answer keys, teacher's notes, alternate-language blocks and metadata that ships but must not show. The parser never looked at it.
- Fix: `!important` was stripped from only twelve kinds of CSS declaration, so the rest lost out to rules that should not have beaten them. Fix: a footnote reference styled on the link itself, rather than wrapped in a `<sup>`, was drawn full-size on the baseline. Fix: Korean text written as combining jamo was not composed into syllables.
- **Fix: looking a word up selected fragments rather than words** (#206). With bionic reading on, "reading" offered "read" and "ing" separately; a word hyphenated across a line break offered each half. Neither looked up. A word broken at its own hyphen resolves either way now.
- **Fix: an image could be replaced by its alt text and stay that way** (#249). Reading an image's dimensions is refused when memory is short, which is meant to be a statement about one moment — but on the main path the refusal was written into the chapter's cache, where it survived page turns and re-entry. Changing the font was what brought the picture back, which is how this was found. The reader now frees what it can and tries again before giving up.
- Fix: a chapter was rebuilt from scratch after a single failed allocation, and an SD-card font could lose an entire style — and so every letter on the page — for want of one block of memory slightly smaller than the one asked for. Fix: a partly-extracted cover is decoded rather than discarded.
- Fix: the header could clip a long label on the right. Fix: text inside nested tables was dropped. Fix: a failed archive read was treated as an enormous one.

### Library, menus and transfer

- **Settings and the reader menu are now tabbed** — Navigation, Settings, Sync and Tools in a book; Display, Reader, Controls and System outside it — instead of one long list. The design is adapted from **CrossInk** by uxjulia.
- **Fix: one press in the file browser ran two handlers.** Confirm entered a folder and then immediately opened the first thing in it; Back always left the browser however deep you were, because the short-press "up one folder" branch could never be reached.
- Fix: picking a firmware file from a folder with 64 or more files in it listed the books next to them, and picking one handed an EPUB over as firmware.
- Fix: a submenu could contain itself. Fix: a settings row did not repaint after being toggled. Fix: taps on weather search results did nothing.
- **New: USB Drive on the X4 Pro and LilyGo T5 S3.** The card appears on a computer as an ordinary removable disk, so books go on and come off with the file manager. On those devices it replaces USB Transfer, which had nothing left to offer once a computer can mount the card directly.
- **Security fix: the web file browser could be walked out of its protected folders.** The rules that keep dotfiles, `System Volume Information` and the cache folder off-limits look only at the last part of a path, which is only sound once `..` has been resolved — and six handlers never resolved it. Download, delete and upload were all affected, and an uploaded file name carrying its own separators landed wherever it liked.
- Fix: Wi-Fi went into power-saving during OPDS browsing, book downloads and KOReader sync, which is where it hurts most. Fix: a screenshot over the cable arrived truncated.

### Under the hood

- Images and covers are noticeably quicker: a stored PNG is decoded where it lies instead of being unpacked first, both pixel caches are written from one pass, writes to the card are buffered, the cover's metadata parse is remembered, and the extracted-image cache is keyed so it survives a layout change.
- A chapter's anchors are streamed to the card rather than held in memory — the single largest chapter-sized allocation in a build — and the footnote preview index now lives on disk, which lifted the cap that made books with more than about five hundred notes give up part-way through. Style lookups skip work that could never match.
- SD-card font glyphs are read straight from the flash mapping instead of being copied into memory a page at a time, which is what was competing with the image decoder above.
- Fix: the home screen retried a 48 KB allocation that could not succeed, once per redraw, for the rest of the session.

### With thanks to

Much of this release is ported from, or built on, other people's work. Where the code is theirs it is credited in the source file it lives in; where the idea is theirs and the code is ours, the source says that too.

- **uxjulia** (Julia Nguyen) — USB mass storage, from the firmware activity down to the USB handoff (crosspoint-reader #3203, freeink-sdk #36/#53/#57), with Justin Mitchell and Uri Tauber; the footnote-reference styling fix (#3355); and **CrossInk**, whose icon-above-label tabbed reader menu this release's menus are modelled on, and whose guide-dots reading aid and table column sizing were earlier borrowings.
- **Bryan O'Sullivan** (@bos) — the diagnosis, encoding and measurements behind the X3 grayscale banding fix (crosspoint-reader #3469; freeink-sdk #94/#95).
- **Justin Mitchell** (@itsthisjustin) — per-device firmware assets and the wrong-device guard (#2880, #2983).
- **Uri Tauber** (@uritauber) — freeing dictionary font caches on exit (#3317); firmware download links on pull requests (#3389).
- **Sung-jin Brian Hong** (@serialx) — Hangul jamo composition (#3036); retrying the glyph arena smaller instead of dropping the style (#3126).
- **Sylve** (@s0lness) — found and fixed the web file browser path traversal (#3353).
- **Joe Harpham** (@jjharpham) — the HTML `hidden` attribute (#3390).
- **Foulad** (@sfoulad) — the failed archive read (#3244); Wi-Fi power-save during downloads (#3252).
- **Phạm Bình An** (@brianhuster) — `!important` on every declaration (#3221).
- **Jadehawk** (@jadehawk) — keeping Wi-Fi awake through KOReader sync and authentication (#3233).
- **adiskill** — reaching the reader menu with touch page turns off (#3319).
- **FDKevin** (@fdkevin0) — the clipped header label (#3235).
- **YUN-JIE** (@YuunJiee) — moving the settings category table into flash (#3364).
- **Antoine Aflalo** (@Belphemur) — holding the X4 Pro's peripheral rail through deep sleep (#3215).
- **Oliver Freyermuth** — memory plotting for the S3 devices.
- **jetaudio** — panel, light and sleep bring-up for the T5 S3 (freeink-sdk #51).
- The screen repair sequence is modelled on `repair()` in **azw413/lilygo-t5s3paperpro-rs**, an independent driver for the same panel family.
- The `freeink-sdk` this firmware sits on is maintained upstream by the Free-Ink project. This release takes several resyncs with it — the largest carrying forty-eight upstream commits — alongside the panel, light, USB and grayscale work contributed back the other way.

### Upgrade notes

- **Download the file that matches your device.** X3 and X4 keep `firmware.bin`; the X4 Pro takes `firmware-x4pro.bin` and the LilyGo T5 S3 takes `firmware-lilygo.bin`. An image meant for another device is now refused rather than installed.
- Chapters are re-indexed once per book the first time you open them, because hidden elements, `!important` and footnote-reference styling all change how a page is laid out. Your place in every book is kept.
- On the X4 Pro and the LilyGo T5 S3, USB Transfer is replaced by USB Drive. The X3 and X4 are unchanged.
- **Any gesture assignments you had customised are reset to the new defaults.** The vertical swipes changed meaning during this cycle — they are anchored to the left and right edge columns now rather than to halves of the screen — so a stored choice named a gesture that no longer exists. Button assignments are untouched.
- The reading light's on/off state is restored on wake only if **Restore Light on Wake** is set; brightness and warmth are always remembered.
- The new touch, gesture, reading-light and diagnostics labels are translated in German, French, Spanish, Italian, Dutch, Portuguese (PT and BR), Polish, Russian, Ukrainian, Belarusian, Slovenian and Swedish. Turkish and Vietnamese show the English text for those rows until a native speaker fills them in; the eight partly-translated languages are unchanged.
- See **[Touch Controls](USER_GUIDE.md#5-touch-controls)** in the user guide for the full gesture reference, or **Settings → Controls → Gesture overview** on the device.

## 2.26 — 2026-08-28

- **Buttons now follow the screen, not the panel.** The four front buttons and the two side buttons sit on different edges of the device, so rotating it swaps which axis each pair moves along — but navigation still assumed portrait. In landscape a list stepped with the buttons beside the screen and jumped pages with the ones below it, the two directions were often reversed, and the hint labels named buttons that did something else. Every list, cursor, keyboard and picker now works out which physical button points the way you mean for the orientation you are holding, and each hint is drawn beside the button that performs it. Page turns, yes/no prompts and your own short/double/long press assignments keep their fixed buttons, as before.
- Fix: held upside down (inverted portrait), the labels on the four front buttons were drawn upside down.
- **Fix: verse and quotations lost their first letters** (#198). Poetry is usually marked up with the indent on the stanza and the hanging indent on each line. Only the first line of a stanza ever picked up the stanza's indent, so from the second line on the text started off the left edge of the screen and its first letter was clipped away. Text could also be pushed off-panel by a negative margin, or by a hanging indent deeper than the indent it hangs out of. Two visible consequences: a line continued with a line break keeps its paragraph's indent instead of getting a fresh first-line indent, and a table inside an indented block is now indented with it.
- **Fix: sleep-screen covers came out nearly black with the "Adaptive" filter.** The filter corrected the picture's own tonal range and the reader then dropped the panel's brightness measurements, so it re-corrected on top — on a sample cover 87% of the image landed in the two darkest levels against 31% with the filter off. Book images were never affected; only the sleep screen.
- **New "Equalize" cover filter** (Settings → Display → Sleep Screen Cover Filter). "Adaptive" stretches a picture between its darkest and lightest points, which does nothing for a cover that is mostly dark with a small bright title — five of seven sample covers came out effectively unchanged. Equalize spreads the tones by how much of the picture actually carries them, so those covers gain real contrast. It is a stronger transform, so smooth backgrounds show a little more dither texture; both are worth trying on your own covers. Applies to inline book images as well.
- **HTTPS connections are now really verified.** Every https request checked the server's certificate and then quietly repeated the request without checking when that failed, so a bad certificate was simply accepted — including on the KOReader sync connection, which carries your sync password. Verification failures are now reported instead. To reach a self-hosted server with a private or self-signed certificate there is a new **Skip HTTPS validation (risky)** toggle under Settings → System; firmware updates stay verified whatever it is set to.
- Fix: signing in to or syncing with some KOReader servers reported a Wi-Fi login page ("captive portal") when the server was simply saying it had nothing stored for that book yet. Servers spell that several ways, and two of them — an empty JSON string and a bare `null` — were being read as a portal.
- **Checking the time is about three seconds faster.** The system clock's time request used to sit in a random delay of up to five seconds before the first packet left the device, on an exchange that takes a few hundredths of a second. The reader now asks directly, and only falls back to the old path if that gets no answer. This delay was paid on every sync, timezone detection and weather update that needed the clock set.
- Wi-Fi connects more reliably: the radio no longer dozes between beacons while connecting, which is what turned a weak moment into a ten-second dead attempt before the retry, and the channel scan is back at its longer dwell after a shorter one made the reader miss access points it could see perfectly well.
- **About 30 KB more memory free**, on a device with roughly 380 KB in total: the Wi-Fi driver's non-critical code is loaded from flash rather than kept resident, two system tasks had their stacks cut to what they actually use, and unused cloud-service components are no longer built in. More free memory means fewer of the restarts and slow rebuilds that a fragmented heap causes. The trade is lower Wi-Fi throughput during transfers, which is a fair swap for a device that syncs a page number and occasionally downloads a book.
- Fix: a PNG uploaded through the web file browser was converted to a JPEG on its way to the card (#196). Only JPEGs are re-encoded now, which is what that step was for.
- "Auto page turn" moved to the top of the reader menu, beside the other reading controls.
- Fix (X4): heavy ghosting after an anti-aliased page. The panel's charge pump was left powered between an anti-aliased page and the next black-and-white one, and could not clear the grey charge at reduced strength.
- Upgrade notes: chapters are re-indexed once per book the first time you open them, because indented blocks are laid out differently — your place in every book is kept. The two new setting labels ("Skip HTTPS validation" and the "Equalize" filter) are English-only in this release; the other 14 languages show the English text until the next translation pass.

## 2.25 — 2026-08-22

- **New: dictionary lookup.** Copy a StarDict dictionary into `/dictionaries` on the card, choose it under Settings → Reader → Dictionary, then look a word up from the reader menu or from any button you map to the Dictionary action. Left/Right steps word by word, the side buttons jump between lines, Confirm shows the definition. Definitions keep the dictionary's own headings and emphasis when there is memory for it and fall back to plain text otherwise. With more than one dictionary installed, Confirm on the definition screen switches to the next one and remembers the choice. Plurals, possessives, verb endings and a dictionary's own synonym list are tried automatically, and a hyphenated compound falls back to its parts ("multi-billionaire" → "billionaire"). A word the dictionary does not have is drawn as an outline before you press anything, so you can see there is nothing to look up. Pronunciation renders properly: Noto Sans now carries the phonetic alphabet, and a font that lacks a symbol draws a close lookalike instead of a row of boxes. Nothing is bundled — dictionaries are files you put on the card; see `docs/dictionary.md`.
- **Lists move faster.** Up and Down step one item; Left and Right now jump a whole screen and keep going while held, so crossing a few hundred chapters or files no longer means holding a button and waiting. Double-tapping Up or Down does the same jump on the screens whose Left/Right are already taken — the file browser, where Options has moved to a long press on Right and the long-press delete is gone (the context menu offers the same thing).
- Long titles in the file browser now wrap over up to three lines instead of being cut off. A folder holding one series used to show the same truncated text on every row, because the part that tells the books apart is the part that got cut.
- Fix: a double-tap on Up/Down registered as two single steps about nine times out of ten — a screen refresh between the two taps was enough to lose the gesture.
- Fix: on a list that fits on one screen, two quick taps threw the selection to the far end instead of moving two rows, and a third tap wrapped it back to the top.
- Fix: on the starred-books screens and the Wi-Fi list, one press did two things at once — it moved the selection *and* triggered the row's action (rename or delete, connect or rescan).
- Recent Books shows covers in a 2×2 grid instead of 3×3. At three columns a cover was drawn about 105×158 pixels, too small to recognise the artwork; the grid now works its size out from the panel, and covers are drawn at their stored size rather than scaled down, which also removes the faint grid pattern that rescaling left in them.
- Fix: a book whose cover is stored in a format the reader cannot decode (an AVIF named "cover.png", for instance) was unpacked from the book again on every pass through the shelf, about 1.3s each time — and a cover that failed also left the *next* book showing as coverless for the rest of the session. Scanning covers no longer writes a helper file to the card for every book it passes, either.
- Fix: opening Recent Books in cover mode could restart the device, as could a chapter whose cache was dropped before it was ever read.
- **Tables** (#186). Tables came out as loose text whenever a cell spanned some but not all of the columns — which is how Standard Ebooks writes row headers, so nearly every table in those books fell back. Three more table fixes came with it: a table that crosses a page break repeats its header row at the top of the next page, a `<caption>` no longer prints below the table it belongs to, and a table keeps its own indentation instead of always spanning the full width.
- Fix: tables could disappear from a chapter completely, and a chapter could be cut short and its shortened form cached, both because memory checks were set far above what the work behind them actually needs. The reader noticed the short chapter and quietly built it a second time, so this also removes duplicated work on table-heavy books.
- Fix: a KOReader or OPDS server address typed with a single slash ("https:/example.com") could never connect, and reported an unrelated redirect error after three timeouts — one slash and two are hard to tell apart on e-ink. Such an address is now repaired as you enter it, and one already saved is repaired at the next boot.
- Fix: the clock in the status bar overlapped the battery percentage when the battery read 100%.
- Fix: an X3 with a USB cable attached could produce no serial output at all for a whole session — the log you are asked for when something goes wrong. Plugging the cable in after boot now opens the log too, instead of needing a restart.
- Pages are built with about 7.5% fewer memory allocations, which leaves the heap less broken up over a long reading session.
- Upgrade notes: chapters are re-indexed once per book the first time you open them, because table cells now record how many columns they span — your place in every book is kept. If you install a dictionary, prefer a plain `.dict` over a `.dict.dz`: the compressed form needs a 32 KB unbroken block of memory that a long reading session cannot always spare (`dictunzip` on a computer converts it).

## 2.24 — 2026-08-20

- Fix: wide images were shown with a large black bar underneath, and other pictures came out stretched or squashed. Six separate problems in how images are sized and drawn are fixed, including a cover that looked right at the top and repeated a single line for the rest of the way down.
- Chapters full of pictures now open straight away. They used to spend up to a minute preparing every image in the chapter, including ones on pages you never reach.
- Fix: pictures could stop appearing for the rest of a book after a brief moment of low memory.
- Fix: some books would not open at all — every chapter failed. They open normally now.
- Fix: Cyrillic text was missing letters.
- Fix: "Normalize font size" had no effect on books that set their text size in a stylesheet. Those books stayed slightly off the size you chose and looked a little blurred. Headings still keep the size their publisher gave them.
- The first footnote in a book no longer sets off "Gathering footnotes" and then "Indexing". Notes are prepared one chapter at a time now, so jumping to a note and back is immediate, and books whose later chapters you never open no longer pay for them. Previews also read better — no stray link or heading text mixed in — and coming back from a note returns you to the paragraph you were reading rather than roughly the right page.
- The reader menu, table of contents, footnote list and bookmarks are about three times quicker to open and to move around in.
- Fixed two freezes that could only be cleared with the reset button: one after a long press — most often when starting a KOReader sync from inside a book — and one when opening the menu, contents, footnote list or bookmarks. Two unexpected restarts around footnotes are fixed as well.
- Fix: waking straight back into a book could draw the page on top of the sleep image instead of replacing it (X4).
- Fix: signing in to a KOReader server failed after the device had been fully switched off, with an error that gave no clue why.
- Fix: the clock on X4 lost time badly. Several ordinary actions — finishing a sync, leaving the web server, downloading a font, installing an update — restart the device quietly in the background, and each one set the clock back to when it last checked the time. It now keeps the time across those restarts.
- Wi-Fi now always connects to the strongest access point. It could previously pick a distant one when several share a network name, take six or seven seconds, or report "no access point found" and quietly try again. Connecting takes about three seconds and behaves the same way every time.
- Fix: on the newer X4 units with the updated screen, the display was upside down and quick page refreshes showed nothing at all.
- Reading statistics are only loaded when you open a screen that shows them, which leaves more memory free while you read.
- The firmware went from almost completely full to about 93% of the space available, mostly by storing the fonts more compactly. Nothing about the fonts themselves changed. This is what made room for the fixes above.
- Upgrade notes: chapters are re-indexed once the first time you open each book, because of the image and text-size fixes above — your place in every book is kept. Pictures are prepared again for the same reason, so the first look at an illustrated chapter is a little slower than the second. If your X4's clock has been badly wrong, it will put itself right the next time the device is online.

## 2.23 — 2026-08-16

- Fix: letters were unevenly thick within the same word (#149). The reader fonts were built without grid-fitting their stems, so an identical stem shipped as 3 pixels of ink in one letter and 4 in the next — at Bookerly 14 it split `b d f i n r u` from the rest of the alphabet. All 40 built-in reader faces (Bookerly and Noto Sans, 10-18pt, four styles each) and 24 of the 28 SD card font families are rebuilt. Letter *spacing* is untouched, so no book repaginates and no reading position moves.
- Waking from sleep is faster and less fussy. The boot no longer runs behind your finger — it used to stop and wait for the power button to be released, and since nothing on screen confirms the press was accepted, people held on, which is exactly what made it slow (measured on X4: 3.9s to a visible page instead of 6.8s). The hold needed to wake is also shorter, and a quick double-click now wakes the device, which it previously refused even though a double-click is what put it to sleep.
- Fix: reading could jump back to the start of the chapter (#147). The anchor a chapter rebuild resolves into was written once when the chapter was entered and then left frozen, so any mid-chapter rebuild — a background build aborting on low memory, a footnote gather, a failed page load — dropped you at the top of the chapter, and the next page render saved that position, so it survived a reboot. The anchor now follows the page you are actually on.
- Fix: a single failed allocation while turning a page deleted the chapter's cache and re-indexed the whole chapter. The cache file is almost always intact in that case; the reader now frees what nobody is waiting on and re-reads the same file, and only rebuilds if that fails too.
- Read-ahead and in-place chapter builds are admitted by a memory check that was asking for more than any build has ever needed, so it could never pass. The check is corrected; the safety margins themselves are unchanged.
- The SD card font "Readerly" is replaced by "Libron" from the same author under the same licence — Readerly was withdrawn upstream and could no longer be built. Its listing also claimed Cyrillic coverage it never had.
- Upgrade notes: the built-in fonts come with the firmware, but SD card fonts are files on your card — Settings → Fonts will now offer an update for every family you have installed, and the fix only applies once you take it. Readerly is no longer in the catalogue; an installed copy keeps working, but Libron is its replacement.

## 2.22 — 2026-08-14

- Background chapter build (read-ahead) now actually runs on device: its memory floors were unreachable — one by 12 bytes, another excluding every book with embedded CSS — correct builds were being discarded as "degraded", and background work ran at the 10 MHz idle clock. A section that took 28s to build now takes ~2s, the up-to-1.2s input freezes during a background slice are gone, and read-ahead waits 1.5s of stillness instead of 4s so it also works while skimming.
- Fewer restarts while reading: the fragmentation caused by a page render interleaving with a live chapter build is fixed (font page slots released, build arena adopted mid-build, arena-aware image decode floors).
- Added web-UI plugins loaded from the SD card (`/.crosspoint/plugins`), with four bundled: metadata-editor (edit title/author/series/description and manage covers, incl. Open Library / Goodreads / Google Books cover search), organize-by-author, find-duplicates and a reference plugin. Plugins run in the browser; a manifest declares which capabilities and remote hosts they may use.
- Added Calibre `book.opf` metadata sidecars: a `book.opf` beside a book overrides its embedded title, author, language, series and description without rewriting the EPUB.
- Added "Adaptive" as a fourth Sleep Screen Cover Filter, applied to BMP and full-screen PNG sleep images and to book covers, with line art detected and left alone.
- Inline images in books now render in grayscale (and get adaptive tone mapping) when anti-aliasing is on.
- KOReader sync: new "On Progress Conflict" setting ("Ask every time" / "Use furthest", changeable from the compare screen), automatic discovery of the document id the server actually uses (KOReader defaults to a content hash, we defaulted to filename — the two never saw each other), an optional sync when a book is finished, and a configurable auto-push page threshold.
- Idle light sleep: the chip now halts between button polls when idle, with unchanged button response. Light-sleep and deep-sleep behaviour is reported on the System Information screen.
- Faster boot (~0.45s off the recovery-combo check) and ~0.5s faster wake straight back into a book on X3.
- Sleep images are now decoded once and cached in a sidecar, instead of three or four times per sleep.
- Books with no real footnotes no longer pay a ~2.8s whole-book scan before the first page.
- Web file browser: images preview in-page instead of downloading, and dropping a folder now uploads its contents with the folder structure intact.
- Fix: an oversized table cell deleted itself and every earlier cell in the same table; tables now lay out row by row, which also removes the limits that made large tables abort a page build.
- Fix: the e-ink panel and the SD card share one SPI bus but only storage locked it, so an interleaved refresh and SD read could corrupt data or panic the system.
- Fix: a failed footnote gather recorded "this book has no footnotes" permanently, surviving reboots.
- Fix: finishing a book synced the start of the last chapter instead of the end.
- Fix: applying remote KOReader progress resolved the position correctly and then discarded it.
- Fix: KOSync now accepts any 2xx response from compatible servers.
- Fix: watchdog-triggered resets are now reported as crashes instead of rebooting silently.
- Fix: X3 reported USB connected and showed the charging icon when detached with a full battery.
- Fix: the web Settings page could stall or trip the watchdog by opening three simultaneous connections.
- Fix: replacing a book cover by hand had no effect, because the cached thumbnail was never invalidated.
- Fix: a finished book left its metadata sidecar behind when moved to /COMPLETED.
- Fix: sleep and wake button handling now agree with each other.
- Hardening: stored credentials (Wi-Fi, KOReader, OPDS) are decoded with a bounded buffer.
- Translations: the 13 strings that were still English-only (the new KOReader sync and sleep settings, the adaptive filter, "Normalize font size", NTP server and the new System Information rows) are now translated in all 15 fully-maintained languages — German, French, Spanish, Italian, Dutch, Portuguese (PT/BR), Polish, Russian, Ukrainian, Belarusian, Slovenian, Swedish, Turkish and Vietnamese.
- Upgrade notes: book covers are re-generated once (they are now cached as 8-bit greyscale so they can be tone-mapped), sleep images gain a small hidden `.pxc` cache file beside them, existing KOReader configurations keep "Ask every time" while new installs default to "Use furthest", a `book.opf` beside a book now takes precedence over the book's own metadata, and web plugins run unsandboxed on a web server that has no authentication — install only ones you trust.

## 2.21 - 2026-08-03
 - Recognition of newer X3 / X4 batches with alternative display controller
 - Implemented "Normalize font size" as a user setting that snaps publisher near-body font wrappers (e.g. around whole paragraphs) back to native 100%
 - Fix: The webserver settings screen could be mangled if the koreader password contained special character
 - Fix: Koreader server authorisation and registration could fail under heavy memory load
 - Fix: Font download could fail under heavy memory load
 - Fix: OTA update could fail under heavy memory load
 - Fix: The involuntary "wake on short power button press" has been resolved
 - Fix: Relative image sizing recognizes outer blocks

## 2.20 — 2026-08-02

- Major page-render speedup on styled books (most commercial EPUBs set a body font size): X4 page render ~450ms → ~50-100ms, X3 ~950ms → ~545ms.
- Page turns feel snappier — the anti-aliasing touch-up is now dropped when you navigate away mid-render instead of finishing it first.
- Fixed a freeze (up to ~28s) finishing a book that has sequels in the same folder.
- Faster library browsing, cover thumbnails, Wi-Fi/web file transfer, and opening of very large books.
- Fixed crashes/failures opening very large books (1700+ chapters) under low memory.
- Fixed a blank cover/title page on books with SVG-wrapped cover art.
- Added drop-cap support (large stylized first letters).
- Fixed button input on the "book finished" screen, dropped page-turns on side buttons, and a footnote-list double-skip.
- Left/Right buttons and labels now work correctly in the reversed (rotated) mounting orientation.
- X3: fixed the charging icon sticking on after unplugging, and the SD card not powering down in sleep (battery drain).
- More reliable Wi-Fi connects (DNS stall fix) and better AP selection when several share an SSID.
- Known issue: newer X3/X4 hardware batches with updated display panels aren't supported yet.

## 2.10.1 — 2026-07-17

- Fixed some EPUB3 books (common Calibre/Pandoc output) failing to open due to a metadata-parsing regression introduced in 2.10.

## 2.10 — 2026-07-17

- Reintroduced progressive JPEG support and reduced image-decoder memory use.
- Added guide dots and inline footnote previews from a book-level cache.
- Added configurable OPDS download folder/filename format.
- Fixed a crash entering deep sleep on the web-transfer screen, an AP low-memory crash on X3, and an out-of-bounds page bug on fast page-turns.
- Fixed ZIP scanning for large/fragmented EPUBs and reduced CSS-related OOM risk.
- Faster shutdown; ~3KB flash saved via zopfli compression.

## 2.09-freeink — 2026-07-14

- Switched to the official freeink display SDK (from open-x4-sdk).
- Fixed several X4 ghosting issues (popup and section-crossing).
- Added an optional X4 display overclock for faster screen refresh, and SDK/version info on the System Information screen.

## 2.09 — 2026-07-14

- Fixed a cover-thumbnail deadlock on a corrupted cached cover image.
- Fixed dither-band artifacts in JPEG decoding.
- Kept full CPU speed while home-screen covers are still resolving (faster library home screen).
- Fixed the web server blocking book listing for Calibre-connected devices.

## 2.08 — 2026-07-13

- Fixed a sporadic missing page update.
- Folded the X4 anti-aliasing pass into the page-refresh window (fewer/faster refreshes).

## 2.07 — 2026-07-09

- Cut first-open time on large books via buffered EPUB cache I/O.
- Fixed secondary-framebuffer release during first-open book indexing.
- Completed the Swedish translation.

## 2.06 — 2026-07-08

- Added support for different font sizes and improved font scaling/reuse.
- Fixed KOReader sync (KOSync) failing under low memory.
- Fixed an OOM reboot from loading the printed-page list all at once.
- Fixed sleep-screen cover Fit/Crop display issues.

## 2.05 — 2026-07-02

- Improved font upscaling quality.
- Added support for rendering large embedded covers on the sleep screen.
- Added an optional physical page-number display.
- Fixed several ghosting issues (indexing popup, X4) and a classic-theme list display bug.

## 2.04 — 2026-06-29

- Added a live "page X of ~Y" estimate while a section is still building.
- Added a book-keyed HTML cache to skip re-inflation on reopen.
- Fixed reader resuming at the chapter start instead of the saved position.
- Re-rendered UI fonts with a native monochrome rasterizer for crisper text.

## 2.03 — 2026-06-26

- Fixed tall images not being sliced properly.
- Fixed a display-refresh race that could cause ghosting.
- Hardened OTA update-metadata fetching; added low-memory safety nets.

## 2.02 — 2026-06-24

- Reworked the web server to release memory properly (stability).
- Fixed anti-aliasing/half-refresh behavior when the sunlight fix is active.
- Fixed a couple of settings submenu bugs.

## 2.01 — 2026-06-24

- Fixed an XML parser regression that could cause books to fail to open.

## 2.00 — 2026-06-23

- Added USB serial file transfer (MicroReader-compatible), with host-side Total/Double Commander plugins.
- Made serial downloads reliable (fixed dropped bytes on transfer).
- Added "more books by this author" lookup and a Power-button shortcut to select a footnote.
- Fixed CSS parent/child margin collapsing.

## 1.99 — 2026-06-18

- Added file-browser sorting (name/date/size/type) and an options menu; bounded-RAM browsing for very large folders.
- Added smallcaps support and persistent file timestamps (RTC-backed).
- Fixed a downscaling darkness issue and carousel-exit ghosting.
- Improved cover-thumbnail handling and background slicing for carousel covers.

## 1.50 — 2026-06-15

- Replaced the JPEG decoder (TJpgDec), fixing several JPEG/GIF decoding bugs and X4 ghosting issues.
- Improved rendering speed for image-heavy documents and CSS-heavy books.
- Reworked the recent-books grid view and fixed stale highlights in it.
- Unified the input handling for TXT/MD readers with the main reader.
