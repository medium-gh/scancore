# Changelog

Downloads for the current version are listed on the [releases page](https://github.com/medium-gh/scancore/releases).

## 2.16.6 — 2026-10-30

What people are reading and watching, and what they are saying about it.

- **Latest**, a new page in the sidebar (on phones under More): this week's trending comics, anime and novels on scancore.onl, ranked by how many people there read or watched them, and the newest comments and reviews as they are posted. The feed updates by itself while the page is open, and sits beside the trending titles on wide windows. A trending anime opens on its AniList page, a comic asks which source to read it from, and a novel opens where it is published. On your own sync server the page says it is only on scancore.onl.

## 2.16.5 — 2026-10-29

Scrolling on phones, a crash when downloading, anime downloads from the info page, and Aniyomi backups.

- **Scrolling on Android** works as the system's own lists do. A short swipe used to count as a tap: it pressed the setting, switch, menu or row under the finger and the page did not move, and pulling down at the top of a page tapped whatever was there. Now a finger that moves more than Android's own touch distance is scrolling, and only a touch that stays put is a tap.
- Downloading an episode could close the app (issue #32). It no longer can, and a download that cannot be queued says so in the log instead.
- **Download from the anime's page**: the download buttons on an AniList anime's page no longer wait until it has played from an extension. Without a source picked yet, downloading asks where to download from (the player's "Watch from"), then starts.
- **Aniyomi backups**: anime still waiting to be found are listed as anime, with the episodes watched, not as comics. They keep the extension and address they came from, so once that extension is installed, "Find them" brings each one in where it was, with the episodes watched. Another version or language of the same extension is tried too. Anime with no tracker link are looked up on AniList ten at a time and spaced out for AniList's limits, so a library of hundreds of anime is matched in a few minutes, where only the first 30 were looked up before. The backup's anime and comic extension repositories are offered when it is brought in.
- **Reviews**: the stars fill as you pick them (hovering previews), and your own review is edited instead of added a second time.
- Signing out no longer removes the next sign-in, and a session ended on another device (or on the website) signs the app out instead of leaving it half signed in.

## 2.16.4 — 2026-10-27

Forgot your password? Reset it without leaving the app.

- **Forgot password?** sits beside the password when you sign in (the welcome screen and Settings → Sync). Enter your email and scancore.onl sends a 6-digit code: type it into the app and choose a new password right there. The same email has a link that works on any device. The code and the link work once and expire in 30 minutes, and five wrong codes use the reset up. On your own sync server, the app tells you to ask whoever runs it.
- **Safer passwords**: new passwords that appeared in known data breaches are refused, and so are passwords built from your username or email. The check is private: only five characters of a hash leave the server, never the password. The strength meter shows what is still missing.
- The sign-in form eases between signing in, creating an account and each recovery step instead of jumping, and a focused field or button brightens its own edge instead of drawing a ring over what is next to it.

## 2.16.3 — 2026-10-27

Copy a page, and pin your sources.

- **This page**: copy a comic page as a picture, save it, or share it on Android. Right-click the page on a computer; on a phone, use More → This page…, or hold the page when Settings → Reading → Long press to zoom is off. A saved page keeps its own file (a JPEG stays a JPEG), named after the series, chapter and page.
- **Pinned sources**: pin an extension on the Extensions page, one of its sources, or a built-in source in Settings → Sources. Pinned ones come first in the source chips on Comics and Novels, in Anime's sources and in Settings, and pinned extensions come first on the Extensions page.
- Long menus (an episode's servers, for one) scroll inside the window instead of running off it.

## 2.16.2 — 2026-10-27

Anime fixes.

- **Continue watching**: Home's Continue row lists every show you're still watching again. After importing a list from MyAnimeList or AniList it could stop at a handful, and shows came and went as you synced.
- **Library**: an anime opened from the Library loads its page (episodes, details, In library) instead of saying AniList has no entry for it. Resuming from the search palette and sharing a list with friends had the same problem.
- **Covers**: when AniList replaces an anime's cover, the old picture stops loading. Covers in the Library and Continue are now fetched again, and anime saved without a cover get one.
- **Player servers**: on desktop, when an episode has more servers than fit beside the title (extensions such as Anikoto list many), they fold into one button with the server in use that opens the whole list. Long menus scroll instead of running off the window.

## 2.16.1 — 2026-10-27

Echo music extensions, and fixes.

- **Echo extensions**: add an Echo extension list (its link or code) in Extensions, and its music extensions' songs show under Music → Extensions: search, their home feed, playback and lyrics. Each extension is signed by its author, and an update must keep that key. On Android the Echo part (under 1 MB) downloads with your first Echo extension. Echo's library is under its own GPL-3.0-based licence and ships only in that download and in the desktop Extensions runtime; SCANCORE itself stays MIT.
- **Series and anime pages**: switching between Chapters (or Episodes), Reviews and Discussion no longer throws you back to the top on phones: the tabs stay where they were.
- **Android**: no more crash report when SCANCORE closes itself (after an update or a restore, for one).
- **Strip reader**: the scroll bar follows the page you are on instead of jumping as pages load, and dragging it goes to a page.
- Home, Library and History skip their refresh work when nothing changed, which helps big libraries on phones.
- "1 chapter" and "1 episode" instead of "1 chapters"; a series page opened from a plain file path (C:\… or /…) works.
- The release no longer fails to publish over the smoke tests' leftover files.

## 2.16.0 — 2026-10-27

CloudStream extensions, your own OPDS and Jellyfin servers, a home-screen widget, FB2 and MOBI books, a new Wrapped, and a steadier strip reader. The last 2.x feature release: fixes come as 2.16.x until 3.0.

- **CloudStream extensions**: add a CloudStream repository (its link, a `cloudstreamrepo://` link or a repository code) in Extensions, and its plugins' sites show under Anime like any other source: browse their sections, search, open a title with its seasons and dub or sub, and play from every server they find, with subtitles. On Android the CloudStream part (about 4 MB) downloads with your first CloudStream extension. CloudStream's library is GPL-3.0 and ships only in that download and in the desktop Extensions runtime; SCANCORE itself stays MIT.
- **Your servers**: OPDS catalogues (Calibre-web, Kavita, Komga, Ubooquity, Codex) in Settings → Sources → Your servers: their comics and EPUB books are sources like the others. A Jellyfin server's shows and films play under Anime, with resume points kept in step both ways.
- **FB2 and MOBI books** (also AZW3, AZW and PRC) open in the novel reader, from a file or a folder you added.
- **Home-screen widget** (Android): continue reading and watching, and how many new chapters wait, in your wallpaper's colours.
- **Music in the system's now playing**: cover, time and a seek bar on Android's lock screen and media controls, MPRIS on Linux, and Windows' and macOS' media controls.
- **On-device translation and sharper scans on Android**: Settings → Reading downloads ONNX Runtime the first time, for translation without Google services and Enlarge downloaded pages.
- **Wrapped, redone**: four styles, slides you can hide, titles and covers you can keep private, a music slide, and a card to save as a story, square or wide picture, with up to six stats you pick.
- **Strip reader**: no more jump or black flash when a page above the screen finishes loading, and the next chapter is fetched again when the first try fails.
- Faster: the Extensions page with hundreds installed and Settings → Sources open at once, Library refreshes skip what did not change, and read aloud starts only when used.
- Long translations fit on phones: Wrapped's card, the tab bar (Portuguese, Turkish, Vietnamese) and the reader layout cards. On phones, "Settings → Sources" links open Sources itself.
- Fixed: Wrapped's Customise changed nothing; Wrapped's month letters in Japanese, Korean, Chinese and Vietnamese.

## 2.15.0 — 2026-10-26

Music: anime songs, your own files and music extensions, with lyrics that follow along.

- **Music** is a new section (a preview: turn it on in the welcome's first step or Settings → Interface). It plays on while you read or watch, from a mini player above the tabs, and in the system's media controls.
- **Anime songs**: openings, endings and inserts from AnimeThemes. Search by song, singer or anime, play this season's by anime, or open the **Songs** tab on an anime's page.
- **Your files**: pick folders of MP3, FLAC, Ogg, Opus or M4A files. They're listed album by album with their covers; nothing is copied. Android reads folders chosen in the system picker.
- **Music extensions**: add a repository of music extensions in Extensions and their songs show under Music → Extensions. SCANCORE doesn't come with any; how to write one is in docs/music-plugins.md.
- **Now Playing**: the cover over a backdrop in its colours, a scrubber, shuffle and repeat, and the queue. Synced lyrics light up line by line and a tap jumps there. They come from a .lrc file next to your file, lyrics kept in the file, the extension, or LRCLIB.
- **Watch the video**: an anime song's creditless video plays beside its lyrics, from where the song was.
- Retro's hub gets a Music section, and the welcome asks what you read, watch and listen to.
- Fixed: in the comic reader on phones, the previous-chapter button ran into the close button. When the top bar is too narrow, reactions, comments, another device and full screen move into a More menu.

## 2.14.1 — 2026-10-26

- **Android player**: the picture no longer freezes, with the app, when the next episode starts on some phones (#30). Video is decoded by the phone's hardware with its frames copied back; Settings → Anime → Picture → Video decoding keeps the faster direct way to choose.
- **macOS**: the release can now be signed and notarized by Apple (#31), so the app opens without a trip to Privacy & Security. This starts once the project has an Apple developer account.

## 2.14.0 — 2026-10-26

A new welcome: real covers, what you read and watch, your Mihon library in one step, and a look you can see before you pick it.

- **Opening**: the mark draws in over drifting covers of the most followed comics and trending anime, then settles into the first question.
- **What do you read and watch?** Comics, novels and anime as three big toggles. The tabs, the sidebar, Home and Retro's hub follow, and Settings → Interface → Sections brings any back.
- **Coming from another app?** Pick Mihon, Tachiyomi, Komikku, Aniyomi or LNReader: the backup, Mihon's downloads folder and the extensions your series came from, in one card that counts what is coming over.
- **Pick a look** beside a live preview of Home or Retro's hub that changes with the layout, theme, accent and glass.
- **Try the reader** on a short sample chapter of our own: scroll it or turn its pages, and see the progress bar you pick on it.
- **Extras** as three short cards: trackers, the scancore.onl account and notifications. The last step sums up your answers, each a tap away from changing it.
- Swipe between steps on phones, tap the dots, use the arrow keys or Back; on a TV the remote's D-pad moves between controls. Standard and Retro each have their own layout, on phones, tablets and computers.
- Fixed: in most languages the light theme was called "thin" (the font weight) in Settings.

## 2.13.2 — 2026-10-26

Search Comick your way, and Anime4K's Best looks right on Linux.

- **Search filters**: the funnel beside the Comics search box opens Comick's own filters: sort, status, type, demographic, content rating, genres and tags to include or leave out (every one of Comick's tags can be searched), years, chapter counts, rating, how recently a series was added, and fully translated. Filters stay until you clear them, and with some set an empty search browses by them.
- **Sharpen (Anime4K)** is also in the player's settings while watching, to compare. On Linux, Best no longer turns the picture teal and checkered: SCANCORE now draws with desktop OpenGL where the computer has it. Phones offer Off and Fast.

## 2.13.1 — 2026-10-25

Back does what you expect in the anime player's own menus and on the welcome screens.

- **Anime player (built-in servers, Android)**: Back closes the player's own menu (quality, subtitles, speed) first, also in full screen, instead of closing the whole player.
- **Welcome screens**: Back goes to the previous step. On the first step it leaves SCANCORE in the background, as on any first screen, or closes the welcome when Settings opened it again.
- **Sources** show their catalogue right below the source chips when you pick one. Thanks to Fpsroxas (#27).

## 2.13.0 — 2026-10-25

Friends, reactions and shared lists; two readers side by side; voices, translation and sharper scans that work on the computer with nothing sent anywhere; and glass.

- **Friends**: Activity → Friends. Follow each other by username on scancore.onl (or your own SCANCORE server) to be friends; once they choose to share it, you see what they read and watch. Sharing is off until you turn it on. Share a library list by link, and add a list someone shared with you to your library in one go.
- **Reactions**: the comic reader's heart reacts to the page you are on, and pages other readers reacted to show as dots beside the page bar.
- **Side by side**: in a wide window, a reader's Read beside opens another comic or novel you are reading on the right half, at the chapter you are on. Keys go to the reader you clicked last.
- **Key profiles**: Settings → Keyboard can keep other reader keys for the series you choose, one-handed keys for webtoons, say. Pick a series' profile in the reader's settings, under Keys.
- **Neural voices on computers**: Settings → Reading → Neural voices downloads natural voices (Piper) that read aloud with no internet. On Android, read aloud uses the best offline voice for the language by itself.
- **Translate on this computer**: download a language once in Settings → Reading, and comic pages and novel chapters are translated on the computer, with nothing sent anywhere (ONNX Runtime, PaddleOCR and Opus-MT).
- **Novels in your language**: chapters from a source in another language, such as WTR-LAB's English novels, can open translated by themselves, and the reader remembers Translate for each series.
- **Sharper downloads on computers**: Enlarge downloaded pages, in the reader's settings, enlarges small scans as they download (waifu2x), on the computer.
- **Komga and Kavita as trackers**: a series you read from another source moves forward on your server too.
- **Mihon import** also offers the extensions you had set up there, even ones with no series in your library (#25).
- **Glass**: menus, sheets and dialogs can show what is behind them, blurred (Settings → Theme) (#25).
- **Retro** page turns now move smoothly instead of jumping to their last frame.
- **Player**: in Stretch and Crop to fill, the picture reaches under a phone's camera cutout. The player no longer waits on mpv to draw, and when the app stops answering it offers a report like a crash report.
- The name is spelt SCANCORE everywhere, including the app's folders. Folders made under the old spelling move over the first time you start this version.
- Fixed, thanks to Fpsroxas: sites' browser checks keep the extension's own User-Agent (#28).
- Not yet checked on real devices: neural voices and on-device translation on Windows and macOS (tested on Linux).

## 2.12.3 — 2026-10-25

Retro's pages show again: Settings and other pages could stay empty in the Retro look.

- **Retro page turns** are back to the way 2.12.0 drew them. The page drawn as one turning picture (2.12.1) could leave the page itself hidden, so a section such as Settings stayed empty under the app bar.

## 2.12.2 — 2026-10-25

Fixes the empty pages of 2.12.1, which was withdrawn, and makes SCANCORE easier to use with a TV's remote. Everything 2.12.1 brought is in this release too (below).

- **Pages show again**: after updating to 2.12.1, Home and every other section could stay empty under the tabs, and the first reader or player opened blank. Page slides and the reader's fade are drawn as in 2.12.0 again; Retro's page turns stay on the drawing thread.
- **Android TV**:
  - Back leaves the player. Before, it only tried to turn the screen to portrait, so the player could not be closed.
  - Drop-downs in Settings no longer take the remote's up and down: the D-pad moves on to the next setting, Select opens the list, and in it the D-pad picks and Select chooses.
  - Settings' list of categories scrolls, so Storage and About can be reached with the D-pad or a pointer remote.
  - A setting whose choices do not fit beside its text puts them under it, instead of squeezing the text into a column a few letters wide (Settings → Connections).
  - Signing in to AniList no longer presents the TV's web view as desktop Chrome, which Cloudflare's check refused.
- Not yet checked on a real TV: the Android TV fixes were tested with the interface on a computer, as a TV with its remote.

## 2.12.1 — 2026-10-24

Withdrawn: its pages could stay empty. Its changes are in 2.12.2.

- **Retro**: the Windows Phone look is now called Retro (it was Metro). Your choice, title, sections, pinned tiles and accent carry over, and so do theme files saved in 2.12.0.
- **Smooth Retro animations**: page turns, the pivot, opening a reader and the standard page slides now run on the drawing thread, so they stay smooth while the page they open is still loading (on phones they could drop to a few frames). The hub switches sections once a swipe settles, and remembering the last section no longer stalls the next swipe.
- **Translation as one story**: with a language model, each page is translated with the pages before it (and what they said), the page after it, the series' title and chapter, and a running summary and glossary of names kept for the series, so a sentence that runs over several bubbles or onto the next page reads as one. It can also show the model the page itself (Settings → Reading → Translate). Novels keep the story between parts of a chapter too.
- **Pick the language in the reader**: while translating, the reader's bar has the language to translate into (on phones, tap the Translate button), and the bar stays up while you pick.
- **Player freeze fixed**: after moving to the next episode the picture could freeze while the sound went on and the app stopped answering (Android). The player no longer waits on the video engine from the interface's thread.

## 2.12.0 — 2026-10-24

A Windows Phone look, comic pages translated where they are, your own comic servers, watching together, and scrolling that keeps up with your finger.

- **Metro**: a new look (Settings → Interface → Look) in the manner of Windows Phone, on phones, tablets and computers. A panorama hub with a big title over a picture from what you read, sections you swipe through (Explore, Recent, Library, Comics, Novels, Anime and more, in the order you pick), tiles in three sizes that flip to show new chapters, pivots, turnstile page turns, an app bar with a button you can hold for more, Selawik type, uppercase or lowercase headers, and Windows Phone's twenty accent colours or any colour from the wheel. Series pages can pin themselves to the hub.
- **Scrolling that keeps up**: on phones a quick swipe now glides the way Android's own lists do, as far as Mihon's; before, a list often stopped the moment the finger lifted (#22, #25). Swipes in a row add up, so long libraries go fast. On computers, two-finger touchpad scrolling moves 2.5 times further (as in GTK apps and browsers) and glides on when the fingers lift. On Linux with NVIDIA graphics, the interface now draws on its own thread, so animations no longer stutter while pages load.
- **Translate comic pages**: the reader's Translate button finds the speech bubbles, translates them and draws the translation over the page, in strip and page mode. On Android it works on the phone with Google's ML Kit (Japanese, Korean, Chinese and Latin-script languages); everywhere it can use DeepL, Google Cloud or any OpenAI-compatible model (a local Ollama works) with your own key, set in Settings → Reading → Translate. Novels can translate a chapter too.
- **Watch together**: start a watch party from the player, share its six-letter code, and everyone watches the same episode from their own source while the host's play, pause and seek steer the others. Chat beside it. Needs a scancore.onl account.
- **Your own comic servers**: add Komga, Kavita or Suwayomi in Settings → Sources → Your servers. Their comics are searched with your other sources, saved to your library, and chapters you finish are marked read on the server too.
- **Webtoons run on into the next chapter**: the next chapter joins the strip as you near the end, with a line naming it, so reading never stops for a button (#9). **Auto-scroll** in the reader's settings (or S), with its speed by your thumb; in page mode the pages turn by themselves. A quick flick now turns a manga page however short it is.
- **Read aloud, rebuilt**: sentence by sentence with the spoken one highlighted, pauses you choose, a voice per language, a sleep timer, and it goes on with the screen off and into the next chapter, with controls in the notification and on headphones.
- **Novels as EPUB**: Save as EPUB on a novel's page makes a book for any e-reader, from the downloaded chapters or after downloading the rest.
- **Spanish**: MangaSin is built in.
- **Phones**: the screen's full refresh rate on ColorOS and realme phones too; readers fill the screen beside a punch hole, with the status and navigation bars hidden while you read (Settings → Reading). The reader's settings are a sheet from the bottom that scrolls (#25).
- **Library**: tapping a list no longer opens its menu on phones; hold it for that (#9, #25). Swipe sideways to move between lists. A Most unread sort. The card about things that need a look can be hidden until there is something new (#25).
- **Computers**: readers and the player open in windows of their own; a magnifying lens under the mouse in the comic reader (L); Android TV runs SCANCORE in the TV's launcher, driven by the remote.
- **Extensions**: Aniyomi extensions that run their own video server play; videos from other players, VLC and casts go through SCANCORE's relay; Extensions → Diagnostics tests a source and lists the sites each extension contacts; the Extensions list keeps its place when one installs (#25). Computers get Extensions runtime 6 by themselves.
- A series' own reader settings now follow you to your other devices; small scans can be sharpened as they are enlarged; the player shows its sources as chips.
- Fixed, thanks to Fpsroxas: a source without settings showed an error instead of "no settings", and opening some sources' settings on computers failed.
- Contributors now appear with their pictures in the README and CONTRIBUTING.
- Not yet checked on real devices: page translation with ML Kit on a phone and with real cloud keys (tested with test engines and on the emulator), the 120 Hz request on a realme GT 6T, Android TV on a real TV, and a live Suwayomi server.

## 2.11.0 — 2026-10-24

Switch from Mihon without losing your downloads, a progress bar you can make your own, and web views for extensions on computers.

- **Mihon and Komikku downloads, read where they are**: choose Mihon's storage folder (or its `downloads` folder) in Settings → Connections → Chapters Mihon downloaded, or right in the backup import. Each downloaded series is matched to the same series from your Mihon backup (by source and title, and by Mihon's own chapter fingerprints when a title was renamed), and each chapter to its chapter. They then count as downloads: they open offline, your progress is kept, and nothing is copied, so 50 GB of chapters do not take another 50 GB. Series not in your library show up under On this device. Works with Android's folder picker too (one quick look per folder: 9,000 chapters are linked in about 6 seconds on a slow test phone), and SCANCORE looks again at each start for chapters Mihon added. The Downloads page shows them as one row per series, and removing one there only stops using the files; Mihon's files are never deleted.
- **The built-in anime player no longer turns into an ad site**: on Android, tapping the player could open an advertiser's page inside it instead of the episode. Pop-ups are refused, the player page cannot be sent elsewhere, and taps on web players now go to SCANCORE's controls (double tap the sides to skip 10 seconds, the middle to pause; hold to reach the site's own menus for a few seconds).
- **A progress bar your way**: three looks for the comic reader's progress bar (a piece per page, one line, or a floating bar with the page number), on the left, right or bottom edge, three thicknesses, your own colours for pages read, unread and current (or one of five sets), and how see-through it is. Settings → Reading shows each one as you change it.
- **Welcome screens**: pick your progress bar (or none) by looking at them, then choose which notifications you want and allow them.
- **Extensions that open a web view now work on computers** (Comix and other newer Keiyoushi sources): SCANCORE downloads the browser they need (about 250 MB, once, the first time such a source is used) and runs it out of sight, as Suwayomi does. Comix's search and chapter lists work on Windows, macOS and Linux. Its chapter pages fail everywhere right now (Android too) after a change on the site; that needs a new version of the extension. Computers get the new Extensions runtime 5 by themselves.
- **Extension updates are easy to find**: Extensions → Installed starts with what is ready to update and an Update all button, or says everything is up to date with when it last checked and a Check for updates button. You get a notification when trusted extensions update by themselves.
- **Browse extensions your way**: under Available, filter by comics, novels or anime, by repository and 18+, beside the language. A repository opens its own list.
- **All sources finds titles in your extensions too**: a search through the catalogue (Comick, AniList or MyAnimeList) now also searches your installed extensions and shows what they have below, as it comes in. Searching many sources at once no longer opens browser checks over the app; a source that needs one gets a button instead.
- **Anime from AniList and MyAnimeList play from the built-in servers** by default, without first searching every installed extension. Settings → Anime → Play from switches to your extensions or to asking, and which extensions to look in. Watch from lets you tick the extensions it searches.
- **Anime pages on wide screens** show the details in a column beside the episodes, each scrolling by itself.
- **Extension log** (computers): Settings → Sources → Extension log opens what extensions reported, errors included, for bug reports.
- Fixed: a second grid of covers lower on a page (such as extension results) stayed empty while scrolling.
- Not yet checked on real devices: Mihon downloads in a real Mihon folder on a phone (tested with Mihon's layout on the Android emulator and on Linux), the extension web view on Windows and macOS (tested on Linux), the ad fix on a phone where an ad appears (the emulator showed none), and Watch from's extension choices with real anime extensions.

## 2.10.0 — 2026-10-23

Lists your way, Komikku backups, a player that stays open, and smoother on slow phones.

- **Built-in lists can be renamed, reordered and hidden** ([#9](https://github.com/medium-gh/scancore/issues/9)): Reading, Plan to read, On hold, Completed and Dropped can't be deleted, because trackers, sync and backups rely on them. You can now give them your own name, move them among your own lists and hide them from the library (long-press or right-click a list chip, or Settings → Library → Your lists). A hidden list's series stay on it and under All. The layout syncs between your devices, and Mihon backups keep the names you chose.
- **Komikku and TachiyomiSY backups** ([#9](https://github.com/medium-gh/scancore/issues/9)): merged entries come in as their main source, series written twice in the backup come in once with everything both copies knew, hidden categories become hidden lists, and edited titles still find their series. Saved searches and edited covers and descriptions stay in the other app, and the import summary says how many. The import dialog and file picker name Komikku and TachiyomiSY.
- **Smoother on slow phones** ([#9](https://github.com/medium-gh/scancore/issues/9)): saving your place while reading made every page behind the reader reload its list. Pages now catch up once when you return. Long chapter lists reuse their rows (about 70% less work per scroll step in our measurements), and a crash when scrolling covers fast is fixed.
- **The anime player no longer closes when you press it**: the hidden back button took clicks at the top-left of the video, and the back arrow on the landscape rail sat where double taps seek. Hidden controls now ignore presses, and the first tap only shows them. The side rail is gone: close is at the top-left, and previous and next episode sit with the other controls. Android's Back leaves full screen before it closes the player.
- **One player settings panel**: Quality, Audio, Subtitles, Speed, the server, episodes and the page's actions (download, cast, other app…) are in one dark panel. It opens as a bottom sheet on phones and scrolls instead of running off a short screen.
- **Fit the picture** (asked on Discord): Fit, Crop to fill (no black bars), Stretch or Original size, from the player's settings or Settings → Anime.
- **Player gestures on phones**: swipe up or down on the left for brightness and on the right for volume, swipe sideways to seek (with a time preview), double tap the sides to skip 10 seconds or the middle to pause, and hold for 2× speed. Each one can be turned off in Settings → Anime → Gestures.
- **Series listed once per uploader** (Kagane): search, "Read from" and moving a series now pick the fullest edition, not the first one found, and results show how many chapters each has.
- **Keiyoushi extensions show their chapters**: Comix and other newer Keiyoushi sources failed with "getMangaUpdate must not be called concurrently for same manga". Details and chapters for a series are now one request. Sources that page through a site slowly get five minutes instead of two. Computers get the new Extensions runtime 4 by themselves.
- **Sources that need Android's web view** (such as Comix) now say on computers that they work on Android, instead of failing with a vague error.
- Not yet checked on real devices: the player gestures (tested on the Android emulator), keeping the screen on with Android, Windows and macOS, the 32-bit ARM (armv7) Android package, and the Quality menu with a real extension's stream.

## 2.9.0 — 2026-10-23

Mihon backups that bring back everything, Kagane, and chapters your way.

- **Extensions on Windows, macOS and Linux** ([#20](https://github.com/medium-gh/scancore/issues/20)): "Browse this source" no longer fails with "Cannot read field mQueue because looper is null". Extensions such as Komga, Comix and others that hand work to Android's main thread now run, and Komga's settings open. Computers get the new Extensions runtime 3 by themselves.
- **Multiple-choice extension settings save** ([#21](https://github.com/medium-gh/scancore/issues/21)): ticking a genre under Blocked genres (or any setting with several choices) failed with "JsonNull is not a JsonArray"; it never saved on any platform. It does now.
- **Settings no longer reset on restart**: settings made of lists (custom keyboard shortcuts, custom theme colours, recent searches, a series' own reader settings and others) were kept only until SCANCORE closed. They now stay.
- **Mihon backups bring back everything**: every Mihon category becomes one of your lists in Mihon's order (empty ones too), with each series' date added, history (so Continue lands on the chapter you read last), every chapter you left part-way on its page, bookmarks, the chapter list's sort and filters, the reading mode you chose for it, Kitsu, MangaUpdates, Shikimori and Bangumi links, and the groups you hid (a new **Hide groups** filter on the series page). Before importing, SCANCORE offers to add the backup's extension repositories and install the extensions its series came from, so they come back on their own sources. A series in several Mihon categories goes to the first, and the import says how many.
- **And back to Mihon**: Back up for Mihon now keeps bookmarks, the page you are on, chapter groups and dates, chapter and reader settings, hidden groups, tracker links and your repositories, and Reading is Mihon's Default. Checked with Mihon 0.21: a library sent there and back comes back the same.
- **Kagane** as a built-in source, read with SCANCORE's own engine, in English and 14 more languages (adult titles left out).
- **Chapters as a grid**: short chapter titles (volumes such as 第01卷191p) can show two to a line on phones and more on wide screens, per series from the grid button above the chapters, or for all in Settings → Reading → Chapter layout. Chapter titles now take two lines instead of being cut off.
- **Volumes, Chapters and Extras**: when a series has more than one kind (卷/巻/Vol. volumes, 话/話/Ch. chapters, 番外/特别篇/Extra/Special/Omake extras), tabs above its chapters show one kind at a time.
- **Hold to zoom**: in the comic reader, hold a finger or the mouse on the page to see it twice as big until you let go, under your finger or at the centre (Settings → Reading). Double tap to zoom can be switched off there.

## 2.8.0 — 2026-10-23

Your own library lists, a quality and dub choice for anime, and the screen stays on.

- **Extensions work on Windows, macOS and Linux again** ([#14](https://github.com/medium-gh/scancore/issues/14)): every extension from Keiyoushi's repository said "This extension uses lib {4:1070386381}, which this SCANCORE does not run" (in Source settings and when loading). The fix was in the app since 2.1.1, but the Extensions runtime that computers download separately was older than it, so it never arrived; that older runtime could not run anime extensions either. SCANCORE now asks for a new runtime and replaces an older one by itself in the background, and a release can no longer go out with a runtime older than its extension host.
- **The screen stays on while you watch or read**: phones no longer dim and lock in the middle of an episode, and computers do not start the screensaver while a video plays (Android, Windows, macOS, and Linux desktops that offer the screensaver or portal inhibit). Readers keep the screen on too; Settings → Reading → Keep the screen on while reading turns that off.
- **Choose the quality** (asked on Discord): Settings → Anime → Preferred quality (best, 1080p, 720p, 480p or 360p) picks which of a source's videos plays first and which resolution a stream with several starts on. A new Quality button on SCANCORE's player lists the stream's resolutions and the server's other videos while you watch.
- **Dubs** ([#15](https://github.com/medium-gh/scancore/issues/15)): Settings → Anime → Preferred audio picks a dub (English, Spanish, Portuguese, French, German, Italian or Hindi) or the original Japanese. Videos and servers are chosen by their names (Dub, Sub, English…), and streams with several audio tracks start on that language.
- **Your own library lists** ([#9](https://github.com/medium-gh/scancore/issues/9)): add lists beside Reading, Plan to read, On hold, Completed and Dropped with New list in the library or Settings → Library → Your lists; rename, reorder or remove them by long-pressing or right-clicking their chip. They sync between your devices, new ones are checked for new chapters, and Mihon and LNReader backups bring their own categories along (and SCANCORE's Mihon export takes yours).
- **Compact grid with titles on the covers** ([#9](https://github.com/medium-gh/scancore/issues/9)): the library's compact grid now shows each title over the foot of its cover, more covers to a row.
- **One chapter list** ([#17](https://github.com/medium-gh/scancore/issues/17)): a comic's or novel's chapters are one list that loads as you scroll, as anime episodes are, instead of ten to a page. Settings → Reading → Show chapters brings the pages of ten back.
- **Reversing the chapter order keeps your place** ([#18](https://github.com/medium-gh/scancore/issues/18)): it no longer jumps to the top of the page, and the page stays at its top while its details load.
- **32-bit Android phones** ([#19](https://github.com/medium-gh/scancore/issues/19)): a separate `android-armv7.apk` for older phones that cannot install the arm64 one, which then updates itself like the other.
- **Open a book file from the Novels page** ([#16](https://github.com/medium-gh/scancore/issues/16)): the folder button opens an EPUB in the novel reader (or a PDF as pages), beside the folders in Settings → Sources → On this device.
- The Flatpak is published again (2.7.1's build pointed at a lexbor tag that does not exist).

## 2.7.1 — 2026-10-21

- **Status bar icons match SCANCORE's theme on Android**: with the phone in light mode, the clock, battery and signal icons were dark on SCANCORE's dark screens and hard to see. They now follow the app's theme (light icons on dark, dark icons on light), including the navigation bar, and the readers and player choose by their own background (the novel reader by its reading theme).

## 2.7.0 — 2026-10-21

- **Anime works while AniList is down**: AniList switched off its service on 21 October, leaving the Anime page saying "Anime details are unavailable right now". SCANCORE now fills the page, anime details, search, episode lists and the schedule from Kitsu and ani.zip whenever AniList does not answer, with the same anime, so your library, progress and sync carry on. Recommendations come back when AniList does. When nothing can be reached, the page shows what it loaded last time, or says why.
- **The MyAnimeList catalogue keeps working too**: its lists and its spotlight fall back the same way when MyAnimeList or AniList is down.
- **A full screen button in the player**: SCANCORE's own video player has one at the end of its controls; on phones it turns the video to landscape.
- **The spotlight's shade covers the whole picture while swiping**: on phones, a bright strip of the banner showed at the edge as you swiped.
- **Sharper comic pages**: pages are decoded at exactly the size they appear on screen and drawn pixel for pixel, instead of being shrunk or stretched by the graphics card. Paged reading is noticeably crisper, and pages no longer go slightly soft between screen pixels while a strip scrolls.
- **Crisper webtoons on large screens**: art narrower than the strip (most webtoons on a computer or a phone) is enlarged with a sharper filter, with cleaner lines and text.
- **Pages sharpen after a resize or zoom**: widening the strip, resizing the window or zooming in decodes the page again at the new size, keeping the old picture until it is ready. Before, a widened strip stayed blurry.
- **Lighter on memory**: paged reading no longer decodes the pages it reads ahead only to learn their shape, and pages use far less video memory.
- **The image cache keeps what you are reading**: when it trims itself it now drops the images you looked at least recently, not the oldest downloads (pages you were still reading could go), and it writes to disk far less while you scroll.

## 2.3.5 — 2026-10-21

- **Extension anime plays again** ([#13](https://github.com/medium-gh/scancore/issues/13)): since 2.2.0 SCANCORE's player turned down most extension videos with "unrecognized file format". It now plays them.
- **Servers that disguise their videos play**: some servers (such as HD-1 on AnimeKai, Anikoto, AniWave and Anichi) send each part of a video dressed up as a picture. SCANCORE now unwraps them on the way to the player, on every platform, and downloads of them play offline.
- **AniZone loads** on desktop instead of failing with an error, and extensions that stream from a server of their own no longer stop about ten minutes into an episode.

## 2.3.4 — 2026-10-21

- **Swipe the anime spotlight**: swipe or drag sideways for the next or previous title, as in most apps. The slide follows your finger, a quick flick is enough, and a swipe that drifts a little no longer scrolls the page instead. (Swiping was meant to work before, but never changed the slide.)

## 2.3.3 — 2026-10-21

- **Reader controls stay out of the status bar**: on phones, the novel reader's top bar no longer slides up over the clock and icons when it hides as you scroll. Both readers now draw under the status bar and keep their controls below it.
- **The novel's title is readable on phones**: it gets its own row under the reader's buttons instead of shrinking to "…".

## 2.3.2 — 2026-10-21

- **Settings → Sources is fast again**: it lists SCANCORE's built-in sources (extensions have their own page), those you have series on first, a dozen at a time with Show all and a search. With many extensions installed it used to build a row for each of their sources.
- **Long lists elsewhere stay light**: the source chips on Comics and Novels, Anime's sources, Add a section, moving several series, Library health, chapter comments and reviews only build what is on screen or show the first ones with Show more.

## 2.3.1 — 2026-10-21

- **Read from opens faster**: a catalogue title is looked up on SCANCORE's built-in sources only, not on every installed extension, and each match shows as soon as its source answers. **My extensions** lists your extensions (recently used first, with a search) and looks on the one you pick; moving a series still asks everything, showing built-in matches first.
- **The Read from list scrolls** when it is long, and opens as a sheet from the bottom on phones.
- **Nothing spills out of its card on phones**: settings buttons beside long descriptions drop below them, wide choices stay inside, and the About, Activity and Sync buttons and tabs fit at 360 px.
- **No more crashes at startup** on systems whose Qt hashes through OpenSSL 3 (such as Arch Linux).
- **Your scancore.onl session stays yours**: the cookies of scancore.onl and your sync server are never shared with extensions, plugins or browser checks.
- The welcome tour says adding a repository is optional: the built-in sources work without one.

## 2.3.0 — 2026-10-21

SCANCORE 2.3 "One story": comic, novel and anime as one story, and a library that looks after itself.

- **The story elsewhere**: a series page shows its other forms side by side (the manhwa an anime adapts, the web novel behind it, every season) with how far you are in each, and opens them in one tap.
- **Where the anime leaves off**: anime pages say where the comic or novel goes on after that season ("Manhwa continues from chapter 46"), and comic and novel pages say where each season ends, with a button to go there. Readers suggest and vote on these on scancore.onl; SCANCORE's own list starts it off.
- **Continue the story** on Home: after you finish a season, or catch up on a comic whose novel is further ahead, a card shows where to go on.
- **Upcoming**: one calendar for what comes out next in your library, anime episodes from their schedule and comic and novel chapters guessed from each series' rhythm (Library → Upcoming).
- **Library health**: sources that stopped answering or updating are listed with a move to another source, and series saved twice on different sources can be merged, keeping everything you read in either. Library shows when something needs a look.
- **Five more trackers**: Kitsu, MangaUpdates and Bangumi (and Shikimori and Simkl in builds with their ids) in Settings → Connections.
- **Read a title from your extensions**: a search result or a series on a built-in source can be looked up on your installed extensions alone.
- **Home is yours to shape**: remove sections you do not want (they no longer load) and add shelves of an extension's latest or popular titles.
- Update checks can send one notification for everything they found (Settings → Library).
- The Flatpak builds again with SCANCORE's own player.
- Android: opening Sources no longer closes SCANCORE on phones without a working text-to-speech engine.

## 2.2.1 — 2026-10-21

- **Adding a second repository no longer closes SCANCORE.**
- **Repository links work however you paste them**: a GitHub repository's page or just owner/name, a raw folder, jsDelivr, Codeberg or GitLab pages, or the link to an index.min.json, repo.json or LNReader plugin list (such as raw.githubusercontent.com/LNReader/lnreader-plugins/…/plugins.min.json). Paste several at once and they are added one after another.
- **AniList connects inside SCANCORE**: sign in and allow it, and the account is connected; nothing to copy. When AniList, MyAnimeList or Comick signs SCANCORE out, you are told and can connect again in one tap.
- **Watching updates your trackers reliably**: an episode counts as watched from 85%, in its last two minutes, once the ending starts, or when you go on to the next one.
- **Continue watching and reading** show the episode or chapter you are really on, say which episode is next, and leave out series you only plan to watch, dropped or finished. Starting a planned series moves it to Watching or Reading.
- **Crash reports**: if SCANCORE closes unexpectedly, the next start offers a report to copy, share or post on GitHub. Settings → About → Crash reports and diagnostics has past reports and a log to send for other problems.

## 2.2.0 — 2026-10-21

SCANCORE 2.2 "Together": your devices and your TV, in step.

- **SCANCORE Wrapped**: your year in comics, novels and anime as a story across the whole window (time spent, chapters and episodes, the series you came back to most and your number one of each, the genres and author you read most, your busiest month and weekday, your longest streak, where your year started) and a card to save as a picture. Home shows it from 1 December to 7 January, and the year review opens it any time.
- **Live progress between your devices**: signed in to scancore.onl (or a SCANCORE sync server 1.1), where you are in a chapter or episode reaches your other open devices within seconds, not at the next sync. **Continue on another device** in the readers and the player sends your exact page or second to one of them, which offers to continue there. Settings → Sync shows which devices are open now.
- **Anime downloads**: download one episode, the next five or every unwatched one from an anime source (or the one playing) and watch without a connection, with its subtitles. Streams are kept as they are, up to 1080p, and a download that stopped goes on where it was.
- **Cast to a TV**: Chromecasts and smart TVs (DLNA) on your Wi-Fi. SCANCORE passes the TV what the video needs, so episodes from extensions and your downloads play there too; the player follows the TV with play, pause, seek and next and keeps your progress.
- **Open in another player**: mpv, VLC or IINA on computers, or MX Player, mpv, VLC and others on Android, with the video's headers, start position and subtitles.
- **Subtitle style** (size, colour, outline, a dark box behind the lines) and **Anime4K sharpening** for lower-resolution episodes, in Settings → Anime.
- **E-ink mode** for Boox, Kobo and other e-paper devices: black on white, no animation, comics in pages with a flash on page turns to clear ghosting, and novels turned page by page with a tap on the sides (Settings → Interface).
- **The Flatpak plays anime from extensions** with SCANCORE's own player, like the other packages, instead of only web players.
- **Four more interface languages**: Polski, ไทย, Filipino and हिन्दी (eighteen besides English).
- Bookmarks and each series' chapter-list settings (sort, filters) now sync between devices too.
- On phones, the player's bar has a menu for the new actions, and the readers' top bars fit on 360 px screens.

## 2.1.1 — 2026-10-21

- **Keiyoushi extensions work again.** Their current builds refused to start in SCANCORE ("CloudflareInterceptor must be present in default client") because SCANCORE's network setup was not exactly Mihon's; on desktop they also failed to load at all. Every one of them runs now, on Android and desktop.
- **Cloudflare checks pass by themselves**, as in Mihon: when a site shows a check, SCANCORE passes it in a browser you do not see and loads the page, usually in a few seconds. SCANCORE now introduces itself as the browser it really is, which is what lets most checks through without a click.
- When a site still wants you to tick the box, the check opens by itself while the page loads: one tap and it closes and the page carries on, nothing to try again. "Open browser check" appears on phones too, and every extension source has "Open the site" in Extensions (to pass a check or sign in), like Mihon's WebView.
- Extensions that open the site in a web view of their own (Comix) no longer wait minutes for a check they cannot see: the first visit to a site in a session passes it first.
- **Adding a repository** no longer has to install anything: "Add only" or "Add and install" the extensions in your language, which then install in the background while you use the app, with progress and a Stop button in Extensions.
- **Search your installed extensions** by name, source or site. Typing in a search box on phones (Extensions, History, a series' chapters) no longer closes the keyboard after every letter.
- On some Linux computers (Intel graphics with an NVIDIA laptop GPU) opening a Cloudflare check, or any page with 3D graphics, closed SCANCORE. It no longer does.

## 2.1.0 — 2026-10-20

SCANCORE 2.1 "Parity": what people coming from Mihon, Aniyomi and Kotatsu reach for first.

- **Move a series to another source**: "Move to another source" on a comic's or novel's page finds it on your other sources (under every name its catalogue knows), shows the match first (chapters, newest, how many will count as read) and moves it with its library list, favourite, tracker links, bookmarks and progress, carried by chapter number. Select series in the library to move them together, and Settings → Sources moves every series of a source at once, including sources that are gone. "Keep the old one too" copies instead.
- **Update rules**: choose which library lists are checked for new chapters, skip finished series, series with chapters left to read or series not started, and check by itself only on Wi-Fi and (Android) only while charging. New chapters of the lists you pick download by themselves, and "Delete chapters once read" clears a download when you finish the next chapter.
- **Chapter tools**: bookmarks, a filter for one translation group, unread, downloaded and bookmarked filters and a sort order remembered per series, "mark previous as read", and Select to download, mark, bookmark or remove many chapters at once.
- **Incognito mode and app lock** (Settings → Privacy): incognito keeps nothing of what you read or watch (no history, place, reading time, tracker updates or Discord status) and shows a badge while it is on. The app lock asks for a PIN when SCANCORE starts and after it has been away (right away, 1, 5 or 15 minutes); on Android fingerprint or face unlocks it too.
- **Comic reader**: a colour filter (brightness, warmth, greyscale, invert), wide spreads split into their two halves in the Pages layout, tap zones (left and right, L shape, Kindle, edges or off, flippable) shown on screen when you change them, volume keys that turn pages on Android, and page bar colours you choose (unread pages very light, read pages grey and the page you are on black to start). On phones the series and chapter title have their own row under the buttons, and the page count in the Pages layout no longer lags a page behind.
- **Backups**: Aniyomi backups restore onto the same anime extension exactly (episodes watched, the one part-way, bookmarks, list), or through AniList when the extension is not installed; LNReader backups restore novels onto the same plugin. Mihon backups now bring categories and bookmarks too.
- **Themes carry your whole look**: a theme file holds light or dark, accent, navigation, spacing, corners, text and cover sizes, motion, the pages you arranged and the readers' colours, and any theme can be exported, not only a custom one. Older theme files still open.
- **Phones**: dropdowns open as a sheet from the bottom, the first-run screen has a cleaner language picker and buttons, the library header takes four rows instead of seven, and Settings shows your scancore.onl profile photo.
- **Long novel chapters** open and scroll quickly: only the paragraphs near the screen exist (a 6000-paragraph chapter opens three times faster).
- **AVIF covers and pages** on Linux, in the Flatpak and on Android 12 and later.
- A repository's GitHub page (for example github.com/keiyoushi/extensions) can be added as an extension repository; SCANCORE reads the repository's own files from it.
- Taps no longer reach the tabs behind the first-run screen.

Still to come: the interface in Polish, Thai, Filipino and Hindi, SCANCORE's video player in the Flatpak, AVIF on Windows and macOS, and bookmarks and per-series settings in sync.

## 2.0.1 — 2026-10-20

- **"Read from" on Windows** found a series only under the names the catalogue gives it, never under the other names a source lists it by, so a series a source lists under its Russian, Spanish or Korean name was missed. It is found again, as on the other systems.
- **Kakao Page** for Korean comics: webtoons and manga with every episode it shows without an account (wait-for-free and paid episodes, and adult titles, need one and are left out). "Read from" opens a series there directly when AniList links to it.

## 2.0.0 — 2026-10-20

SCANCORE 2.0: comics, novels and anime from the extensions you already use, SCANCORE's own video player, pages you arrange yourself, the interface in fourteen languages, and sources in your language. Everything in 2.0.0-beta.1 below is in it, and since the beta:

- **Korean webtoons**: Naver Webtoon, with every episode it shows without an account (paid previews and adult titles, which need one, are left out).
- **Official WEBTOON in six more languages**: Spanish, French, German, Indonesian, Thai and Traditional Chinese, and Dongman Manhua in Simplified Chinese. They come with your content language like the other built-in sources.
- **Much more Spanish**: ZonaTMO, Olympus Scanlation and LectorManga, and ten scanlation groups' own sites (House Of Otakus, InfraFandub, Inventario Oculto, MHScans, Monopoly Scan, Ragnarok Scanlation, RichtoScan, Uchuujin Projects, UnderTranslations, Yuri-Online), each checked from search to the last page image.
- **"Read from" finds series under their translated names**: a source that lists a series as "Lector omnisciente" or "Torre de Dios" is now found from its English name, using the names the source and MangaDex know in your language. When AniList links a title to an official edition (WEBTOON in your language, Naver Webtoon), "Read from" opens that one directly. A spin-off that shares its series' name no longer stands in for the series.
- **MangaDex in other languages was always English** in "Read from": its series show under their name in that language, which never matched the catalogue's. They match now, and a title with no name in your language shows its English name instead of a romanised one.
- Chapter names SCANCORE makes up are in the source's language (Chapitre, Capítulo, Kapitel, Глава, 화, 話 and more).
- **Back up for Mihon** (Settings → Connections): saves the comics you read from extensions, with their categories and the chapters you read, as a .tachibk file Mihon or Tachiyomi restore. Restoring a Mihon backup puts each series on the same extension source when it is installed, as Mihon does.
- **Licences** (Settings → About): the open-source software inside SCANCORE and the full text of each licence.
- Built-in sources can be switched off and back on for everyone without an update, and are a separate part of SCANCORE's code; extensions and your files keep working either way.

Still to come: the interface in Polish, Thai, Filipino and Hindi, exact restores of Aniyomi and LNReader backups, and SCANCORE's video player in the Flatpak (the Flatpak plays anime in its web player). Kakao Webtoon and most Japanese publishers' sites protect their page images, so SCANCORE does not read them.

## 2.0.0-beta.1 — 2026-10-20

A test version of 2.0: comics, novels and anime from the extensions you already use, SCANCORE's own video player, pages you arrange yourself, ten more languages with sources in them, a library per media with categories, notifications, and a faster app everywhere. Export a backup before you install it (Settings → Storage → Back up everything).

- **Arrange every page**: Home, Comics, Novels and Anime have an Edit page button. Drag sections (or move them with arrows on phones), hide the ones you do not use, and give each shelf its own look: a row or a grid, small to large cards, how many items, and whether details and progress bars show. The search bar, source chips, greeting and anime spotlight move and hide too. Settings → Interface → Pages sets the spacing between sections and how round corners are, and resets everything.
- **SCANCORE in ten more languages**: Korean, Japanese, Chinese (simplified and traditional), Portuguese (Portugal), German, Italian, Russian, Turkish and Vietnamese, beside Spanish, French, Indonesian and Brazilian Portuguese.
- **Sources in your language**: built-in comic sources for Brazilian Portuguese, Indonesian, French, Turkish, Thai and Vietnamese, and MangaDex in Portuguese, Indonesian, French, Russian, Turkish, Vietnamese, Arabic, Thai, Korean, Chinese, German, Italian and Polish. SCANCORE shows the sources in your language (Settings → Sources → Source languages adds others), and when none of them has a series, "Read from" offers to look for it in another language.
- **Extensions for all three media**: add a comic, novel or anime extension repository from Settings or the first-run screen, or with one tap on a repository link (scancore://, tachiyomi://, mihon://, aniyomi:// and lnreader:// links open in SCANCORE). The new Extensions page installs, updates and removes extensions, asks before trusting a new signing key, and has each source's own settings. Comic and anime extensions run inside the app on Android; on desktop, SCANCORE downloads a small Java runtime for them the first time. Novel plugins run in SCANCORE itself on every system.
- **Browse any source**: its popular and latest lists and its own search filters, with covers and downloads through the extension. Search shows each source's results as soon as they arrive instead of waiting for the slowest, and "Read from" finds a series on your extension sources too, without being held up by a stuck one. Sites that ask for a browser check open in a browser window (on Android, the system's web view) and continue afterwards.
- **SCANCORE's own video player** for anime from extensions: hardware decoding, HLS streams, subtitles, switching servers without losing your place, the next episode starting by itself, resuming where you stopped, skipping openings and endings that the video marks, and Space, arrow and M keys. **Watch from** plays an anime from the AniList catalogue on one of your anime sources: SCANCORE picks the source when only one in your language has it, and asks otherwise.
- **Catalogues and trackers**: Comics, Novels and Anime each browse the catalogue you choose (AniList, MyAnimeList or Comick for comics; AniList or MyAnimeList for anime), chosen on first run or in Settings → Connections, where AniList, MyAnimeList and Comick connect too. A series page says when a tracker could not find the series and links it in one tap. "Read from" lists sources in your content language first.
- **Library per media**, each with Reading, Planned, Paused, Completed and Dropped, favourites, and a grid, compact or list view.
- **Notifications**: new chapters, extension and app updates and announcements from scancore.onl are kept in a notification area with an unread count, and reach the system's notifications when SCANCORE is not on screen.
- **Full-screen reading** that is remembered, without the status bar or the page bar.
- **The comic reader's page bar your way**: always shown, only with the reader's controls (tap the page), or off, in thin, medium or thick (the reader's settings menu). Drag along it to move through the pages.
- **Anime on Android start with sound and go full screen**: episodes from the built-in servers used to start muted, and the player's full screen button said full screen was not supported. It now turns the phone to landscape without the system bars, and Back leaves it. The full screen button above the player turns the phone too, even with auto-rotate off.
- "Add to library" on an anime saves it and changes to "In library" at once; it said "Removed from your library" and stayed as it was. The library button, favourites and progress on comic and novel pages, and lists of sources and extensions, also update as soon as they change instead of when the page is opened again.
- **Novels**: read aloud with your system's voices, and much more control over the page: fonts, weight, line and paragraph spacing, margins, indent, your own colours, dimming and auto-scroll. A built-in source for a machine-translated novel site (English).
- **Faster**: SCANCORE starts about twice as fast, covers are drawn by one shader and grids create only what is on screen, sections no longer freeze the window the first time they open, and fitted comic pages shrink cleanly. Mouse wheels and touchpads scroll the same way everywhere, with a speed setting in Settings → Interface.
- Covers and pages from servers that sent AVIF images show again: SCANCORE now asks only for formats it can show.
- MangaDex says when every chapter of a series was taken down instead of showing an empty list.
- Continue reading shows a series once, from the source you read it on last, and a saved cover that a site moved is fetched again. Episodes of one anime airing at the same time are one entry in the schedule (Ep 1–10).
- Android: SCANCORE keeps running in the background for downloads, update checks and notifications, and offers to turn off battery restrictions for it.

Not in this beta yet: Flatpak and package-manager builds (use the AppImage on Linux), restoring Mihon, Aniyomi and LNReader backups exactly, the licences screen, and the interface in Polish, Thai, Filipino and Hindi. The video player has been tested on Linux and in the Android emulator so far; on Windows, macOS and phones it is new in this beta.

## 1.5.0 — 2026-10-19

Spanish comic sources, source languages, accent-proof title matching, episodes that open again from an anime's page, and search from the Android keyboard.

- **Comics in Spanish**: five new sources. MangaDex (Español) lists Spanish and Latin American Spanish translations, with Spanish titles and descriptions where MangaDex has them; ManhwaWeb, InManga, LeerCapitulo and MangaOni are Spanish sites. A series read in Spanish is its own library entry, separate from the same series in English. InManga has lost the pages of many older chapters, and SCANCORE says so when you open one.
- **Source languages**: Settings → Sources shows each source's language, and comic source chips show it when it is not English. A new setting keeps "All sources" (and "Read from" for Comick's catalogue) to one language; it searches every language unless you change it.
- **Language on the first screen**: the welcome screen starts with the interface language, so the rest of the intro is in it, and its look step can pick the comic source language. Its Theme, Accent, Comics open as and Reduce motion labels are translated now.
- **Titles match with or without accents**: "Pequeña" and "pequena" are the same title in search, "Read from" and AniList, MyAnimeList and Comick matching. Japanese, Korean and Chinese titles take part in comic search ranking too; they were ignored before.
- **Tracking by MangaDex's links**: comics from MangaDex (either language) are matched on AniList and MyAnimeList by the ids MangaDex gives, not only by title.
- **Spanish chapter numbers**: "Capítulo 12", "Cap. 12" and "Episodio 12" are read as chapter 12, and "Temporada 2 Capítulo 12" no longer counts as chapter 2. Chapter names SCANCORE makes up are in the source's language.
- Tapping or clicking an episode on an anime's page plays it again; it did nothing since 1.4.0. The play button on episode thumbnails is white again instead of a black disc.
- Sites behind Cloudflare that work normally are no longer reported as asking for a browser check.
- Android: pressing Enter on the keyboard in a search field searches, instead of moving to the clear button.
- Android: the comic reader's top bar sits right under the status bar instead of a gap below it.

## 1.4.0 — 2026-10-19

Comments and reviews from scancore.onl in the app, comics from Comick's catalogue, chapter lists ten at a time, the page bar back at the reader's edge, and access codes for new scancore.onl accounts.

- **Comments and reviews in the app**: comics, novels and anime have Reviews and Discussion tabs beside their chapters or episodes, and the readers and the player have a comments button for the chapter or episode you are on. They are the same threads as on scancore.onl, so a comment posted on the website shows in the app and the other way round. Anyone can read them; sign in to your scancore.onl account in Settings → Sync to post, reply, vote and rate.
- **Comics keyed on Comick's catalogue**: the Comics page shows Comick's trending, most popular, new and recently completed comics, and a comic opened from the catalogue keeps its catalogue entry. That ties it to the same comments and reviews whichever source you read it on, and Comick tracking uses it instead of guessing by title. "Read from" also looks for a comic under every name the catalogue knows, so entries from the shelves are found on more sources.
- **Chapter lists ten at a time**: a series page shows ten chapters per page with page numbers and a chapter search (on phones too, where the list used to be one long column), and opens on the page of the chapter you are on.
- **The comic reader's page bar is back at the left edge**: one thin segment per page (read pages dark, unread grey, the page you are on white) that you can tap to jump to a page. The bottom bar with the page slider is gone; the page number is under the chapter title and the previous and next chapter buttons are at the top.
- **Creating a scancore.onl account needs an access code**: the sign-up form shows six boxes for it when the site asks for one ("Contact logesh@mediumdev.com for access"). Existing accounts, signing in, your own SCANCORE sync server, WebDAV and folder sync are unchanged.

## 1.3.0 — 2026-10-18

Activity stats, self-hosted/WebDAV/folder sync, local comics and EPUBs, Comick-powered search, AniList/MAL/Comick tracking, library imports, two-page and zoom in the comic reader, theme gallery, four new languages, and many fixes.

- **Activity**: a new page (Ctrl+8) with the time you spent reading and watching, chapters and episodes finished, your streak, a calendar of active days coloured by what you did, and charts for the last 30 days, 12 weeks or 12 months, on every device you sync. **Your year** sums it up on one card you can save as a picture.
- **Sync without scancore.onl**: Settings → Sync can use a SCANCORE sync server on your own computer, NAS or server ([scancore-web](https://github.com/medium-gh/scancore-web), one Docker command), any WebDAV folder (Nextcloud, ownCloud, a NAS) or a folder that Syncthing, Dropbox, OneDrive or iCloud Drive keeps in step. Each device keeps its own small file and reads the others', so the library, removals, progress and activity follow you without an account, and Settings lists the devices that sync together.
- **Comics and books on your device**: choose folders in Settings → Sources and Comics and Novels list them under On this device: folders of images, CBZ, CBR and 7z archives and PDF files as comics, and EPUB books as novels. They open from your file manager too.
- **Search almost every series**: the comic search asks Comick's catalogue, which knows nearly every manga, manhwa and manhua under all its names, and opens your pick from whichever of your sources has it (it asks which one when several do). Settings → Sources turns this off to search each source directly, as before.
- **AniList, MyAnimeList and Comick** (Settings → Connections): finishing a chapter or episode moves the series forward on your lists there. Anime are matched by their AniList entry and comics and novels by title, and any series can be linked by hand from the new tracking button on its page.
- **Bring your library**: import a Mihon or Tachiyomi backup, a public AniList list, a MyAnimeList export or your Comick library. Anime come in with the episodes you watched, comics with the chapters you read when one of your sources has them, and the rest wait in a list that SCANCORE can search every source for.
- Comic reader, in page mode: **two pages side by side** in wide windows (covers and double-page spreads stay alone), **trimming of white or black page borders**, and **zoom** by pinching, double-clicking the middle of the page or Ctrl + scroll; drag to look around a zoomed page. **Each series can keep its own reader settings** (layout, direction, width, spreads, trimming) from the reader's settings.
- **Theme gallery** in Settings → Theme: eight themes to start from, and anyone can share theme files through the scancore repository. Opening a .sct file from your file manager now opens it in SCANCORE, and SCANCORE appears among the apps that open CBZ, CBR and EPUB files.
- **SCANCORE speaks Spanish, French, Brazilian Portuguese and Indonesian**, following your system or chosen in Settings → Interface → Language, which switches without a restart.
- Account sync no longer starts a second sync a minute after every sync.
- **Fixed parts of SCANCORE working on separate copies of the library, account, downloads and updater**, a mix-up present since 1.0. Automatic update checks now show the update banner (only a check from Settings → About did before), saving or removing a series starts a sync and removals reach scancore.onl, signed-in users no longer sync twice at startup, and changes in Settings, such as how often to check for new chapters or app updates, apply without a restart.
- Opening SCANCORE while it is already running, for example in the tray, brings its window back instead of starting a second copy on the same library.
- Reading keys are yours to change as well: scrolling by a line or a screen (↓ ↑, J K, Space, Page Up/Down) is in Settings → Keyboard, and the novel reader now follows all your reader shortcuts; it ignored changed keys before.
- The novel reader goes back to where you stopped in a chapter that was loaded ahead (it started at the top), counts a chapter that fits on one screen as read, and its menus and messages fit narrow phones.
- The Ctrl+K palette groups its results under Search, Recent, Pages and Commands, and pointing at or clicking a result works again.
- The anime player remembers the server you picked and starts the next episode on it.
- Release pages list what changed and which file to download for each system.

## 1.2.0 — 2026-10-18

- **Your own theme.** Pick the window colour on a colour wheel, with a brightness slider that decides how dark it is, and the whole app follows it. Add a picture or animated GIF behind everything, fitted, dimmed and blurred to taste. Panels can be see-through, and so can the whole window, showing your desktop through it. Accents can be any colour too, and indigo, emerald and amber join the presets.
- **Share themes** as .sct files: one file with the colours, transparency and background picture inside it, which works on every system. Import from Settings → Theme, or drop the file on the window.
- **Settings shows one category at a time** on desktop, with a search box over the list of categories, instead of one long page to scroll through.
- **Navigation your way**: a full sidebar, a floating one, an icon rail or a dock, on the left or right (the dock on the top or bottom).
- **Change any shortcut** in Settings → Keyboard: click it and press the new keys. A key taken from another action moves over, and each one can go back to its default. The shortcuts sheet (**?**) shows yours.
- Settings → Interface adds text size, cover size (fit more covers on screen, or fewer and larger), animation speed (relaxed, default or snappy) and the page SCANCORE opens on.
- **Pick up where you left off.** Home leads with the last thing you read or watched, with its progress and a Continue button.
- **Library sorting**: new chapters first, recently read, recently added, or by title. History has a search box.
- **Recent searches** on the Comics and Novels pages, one click to search again.
- **Quick commands** in the Ctrl+K palette: switch theme, reduce motion, check for new chapters, sync, and a sheet of every keyboard shortcut (also on **?**).
- Comic reader: in page mode, swipe or drag sideways to turn the page.
- Anime: endings can be skipped automatically too, and the next-episode countdown can be 3, 8 or 15 seconds. With intros skipped automatically, the Skip button now still appears for endings, which were otherwise impossible to skip.
- Livelier details: notifications show an icon and the time left, pause while you point at them and can be swiped away; the anime spotlight shows when the next title comes, pauses on hover and can be swiped; shelves fade at the edge that scrolls; long pages have a back-to-top button; dialogs spring in.
- Lists no longer blink or replay their entrance when the library changes in the background, for example after a sync or while chapters download, and the Downloads list keeps its place during downloads.
- Signing out asks first.
- The sidebar scrolls instead of overlapping its Settings button in short windows.
- The logo on Settings → About is no longer stretched.
- **Anime plays on Windows, macOS and Linux again.** Episodes are H.264 video, which the browser built into the desktop downloads could not decode, so the player only ever showed a spinner (Android was unaffected). Windows now plays through Microsoft Edge WebView2 and macOS through the system's WebKit. The Flatpak gains the player, using Flathub's Qt WebEngine, which has the codec, and the AppImage is built with a Qt WebEngine that has it too. Where a build still cannot play a video, the player says so and offers to open the episode in your browser.
- The player has a button to open the episode in your browser. On Windows and macOS the episode list sits beside the video, and in full screen the controls come back when the pointer reaches the top edge.
- **Sync is lighter.** While you watch or read, changes are sent to scancore.onl at most once a minute instead of every few seconds, and saves the website does not recognise are no longer offered again at every sync.
- Series you remove from your library are removed on scancore.onl too, instead of coming back at the next sync.
- A sync started from Settings can no longer stop halfway and leave "Syncing…" showing until a restart.
- Settings → Account no longer lists what syncs.
- Phones: page titles keep their full width (their buttons move below them), download progress shows its text again, and the anime schedule's day picker, search field and long titles fit the screen. Dialogs and reader messages fit narrow screens.
- Anime Browse shows a Load more button when the first results do not fill the page.
- The mouse wheel scrolls the page again with the pointer over a row of covers (Home, Anime, Novels and more); the row kept the wheel for itself, most noticeably on Windows. Shift with the wheel, or a sideways wheel, still scrolls the row.
- Comic pages from sites that protect their images (Webtoon among them) could stay black when a chapter opened, the first page in page mode most of all: the page was requested before the reader knew which site it came from. They load now, and a page that fails in page mode says so and offers Retry, as the vertical strip does.
- The Downloads page no longer measures the whole downloads folder on every page that downloads, which made it stutter during large downloads.

## 1.1.0 — 2026-10-17

- **Back works again.** Esc, the close arrow and Android Back now leave the comic reader, novel reader, anime player and browser checks; they did nothing before. Esc also goes back from details pages, and the back button on mice works.
- **Comic reader controls stay out of the way.** They no longer reappear on their own while you read or scroll; move the mouse (desktop) or tap (phone) to bring them back. On phones the readers now fill the screen behind the status bar, and the comic reader's top bar sits at the very top.
- **Anime progress syncs.** With a scancore.onl account, episodes you watch and where you stopped now sync between your devices and the website.
- **New Settings.** Settings is organised into categories with picture choices for the theme and reader layout, a live preview for novel text, and its own phone layout. Panel gap, progress rail, line spacing, column width and justified text are now adjustable there.
- **Faster to pick up where you left off.** Comics and novels you have opened before show their chapters immediately, and Continue starts reading without waiting for the site. A request that fails because the connection blinked is retried once automatically.
- Android: Back on Home now leaves the app (it stays running in the background), and a tap shows or hides the reader controls.
- Buttons and chips on the accent colour have readable text in the light theme and with coloured accents.
- Phones: source pickers scroll sideways instead of wrapping, the library filter no longer runs off screen, and reader titles show.
- Leaving the phone player in landscape no longer leaves the app in full screen (thanks to @Doomguy-BFG9000).
- Building from source now requires Qt 6.9.

## 1.0.0 — 2026-10-16

- First release in the reset v1 series. This release retains the current SCANCORE code, including the desktop and phone interface improvements.
- Read comics and novels, browse and watch anime, keep a local library and reading history, and download chapters for offline use.
- Linux packaging supports Flatpak-managed updates and can build without the optional downloadable Python novel-source pack.
- Android's numeric build code remains above previously shipped builds so existing Android installations can upgrade despite the public version reset.
