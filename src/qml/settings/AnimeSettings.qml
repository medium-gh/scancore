import QtQuick
import Scancore

SettingsPane {
    title: qsTr("Anime")
    subtitle: qsTr("Playback and skipping")
    iconName: "tv"

    SettingsGroup {
        title: qsTr("Player")
        note: qsTr("Episodes resume where you stopped. Skip times come from AniSkip when someone has submitted them.")
        SettingRow {
            iconName: "skip-forward"
            title: qsTr("Play the next episode automatically")
            description: qsTr("Starts after a short countdown you can cancel.")
            Toggle { checked: App.settings.autoNext !== false; onToggled: App.setSetting("autoNext", checked) }
        }
        SettingRow {
            iconName: "clock"
            title: qsTr("Countdown before the next episode")
            enabled: App.settings.autoNext !== false
            opacity: enabled ? 1 : 0.45
            Segmented {
                options: [{ value: 3, label: "3 s" }, { value: 8, label: "8 s" }, { value: 15, label: "15 s" }]
                value: Number(App.settings.nextCountdown) || 8
                onPicked: (v) => App.setSetting("nextCountdown", v)
            }
        }
        SettingRow {
            iconName: "rotate-ccw"
            title: qsTr("Skip intros and recaps automatically")
            description: qsTr("Otherwise a Skip button appears while they play.")
            Toggle { checked: App.settings.autoSkip === true; onToggled: App.setSetting("autoSkip", checked) }
        }
        SettingRow {
            iconName: "skip-forward"
            title: qsTr("Skip endings automatically")
            description: qsTr("Goes straight to the next episode's countdown when the credits start.")
            Toggle { checked: App.settings.autoSkipEndings === true; onToggled: App.setSetting("autoSkipEndings", checked) }
        }
    }

    // Anime opened from AniList or MyAnimeList: where they play.
    readonly property string trackerPlayer: App.settings.animeTrackerPlayer || "builtin"
    readonly property var matchSources: Array.from(App.settings.animeMatchSources || [])
    readonly property var animeExtensions: { App.dependOn(Extensions.revision); return Anime.sources().filter(s => s.enabled).map(s => ({ key: s.key, label: s.label, language: s.language })) }
    SettingsGroup {
        visible: Anime.playerEngine !== ""
        title: qsTr("Anime from AniList and MyAnimeList")
        note: qsTr("What you pick under Watch from for an anime is remembered for it, whatever is set here.")
        SettingRow {
            iconName: "server"
            title: qsTr("Play from")
            description: trackerPlayer === "builtin" ? qsTr("The built-in servers. Extensions are looked in only when you choose Watch from.")
                : trackerPlayer === "extensions" ? qsTr("Your anime extensions, when one clearly has it; otherwise you choose.")
                : qsTr("Asks the first time each anime plays.")
            Segmented {
                options: [{ value: "builtin", label: qsTr("Built-in") }, { value: "extensions", label: qsTr("Extensions") }, { value: "ask", label: qsTr("Ask") }]
                value: trackerPlayer
                onPicked: (v) => App.setSetting("animeTrackerPlayer", v)
            }
        }
        SettingRow {
            visible: trackerPlayer !== "builtin" && animeExtensions.length > 1
            iconName: "puzzle"
            title: qsTr("Extensions to look in")
            description: matchSources.length === 0 ? qsTr("All of them") : qsTr("%1 chosen").arg(matchSources.length)
        }
        Item {
            visible: trackerPlayer !== "builtin" && animeExtensions.length > 1
            width: parent.width
            height: visible ? 48 : 0
            SourceChips {
                y: 4
                sources: animeExtensions
                ticked: matchSources
                allLabel: qsTr("All extensions")
                showLanguage: true
                inset: 16
                onPicked: (key) => App.setSetting("animeMatchSources", key === "all" ? []
                    : matchSources.indexOf(key) >= 0 ? matchSources.filter(k => k !== key) : matchSources.concat([key]))
            }
        }
    }

    SettingsGroup {
        title: qsTr("Quality and audio")
        note: qsTr("What plays first. A source's videos are chosen by their names, and a stream with several resolutions or audio tracks starts on the ones asked for. Change either while watching from the player's settings (the gear).")
        SettingRow {
            iconName: "settings"
            title: qsTr("Preferred quality")
            description: qsTr("The highest resolution at or under this one; lower ones save data.")
            Select {
                model: VideoPrefs.heights
                value: Number(App.settings.animeQuality) || 0
                onPicked: (v) => App.setSetting("animeQuality", v)
            }
        }
        SettingRow {
            iconName: "languages"
            title: qsTr("Preferred audio")
            description: qsTr("A dub when the source has one, or the original language with subtitles.")
            Select {
                model: VideoPrefs.languages
                value: App.settings.animeAudio || ""
                onPicked: (v) => App.setSetting("animeAudio", v)
            }
        }
    }

    // SCANCORE's own player (anime from extensions); web players draw their own.
    SettingsGroup {
        visible: Anime.nativePlayerAvailable
        title: qsTr("Subtitles")
        note: qsTr("For SCANCORE's player. Subtitles with their own styling keep it, apart from the size.")
        SettingRow {
            iconName: "zoom-in"
            title: qsTr("Size")
            Segmented {
                options: [{ value: 0.8, label: qsTr("Small") }, { value: 1, label: qsTr("Normal") }, { value: 1.25, label: qsTr("Large") }, { value: 1.5, label: qsTr("Huge") }]
                value: Number(App.settings.subtitleScale) || 1
                onPicked: (v) => App.setSetting("subtitleScale", v)
            }
        }
        SettingRow {
            iconName: "palette"
            title: qsTr("Colour")
            Segmented {
                options: [{ value: "#ffffff", label: qsTr("White") }, { value: "#ffe14d", label: qsTr("Yellow") }, { value: "#b8f5ff", label: qsTr("Light blue") }]
                value: App.settings.subtitleColor || "#ffffff"
                onPicked: (v) => App.setSetting("subtitleColor", v)
            }
        }
        SettingRow {
            iconName: "type"
            title: qsTr("Outline")
            enabled: App.settings.subtitleBox !== true
            opacity: enabled ? 1 : 0.45
            Segmented {
                options: [{ value: 1.5, label: qsTr("Thin") }, { value: 3, label: qsTr("Normal") }, { value: 5, label: qsTr("Thick") }]
                value: Number(App.settings.subtitleOutline)
                onPicked: (v) => App.setSetting("subtitleOutline", v)
            }
        }
        SettingRow {
            iconName: "square"
            title: qsTr("Dark box behind the lines")
            description: qsTr("Easier to read over bright scenes.")
            Toggle { checked: App.settings.subtitleBox === true; onToggled: App.setSetting("subtitleBox", checked) }
        }
    }

    SettingsGroup {
        visible: Anime.nativePlayerAvailable
        title: qsTr("Picture")
        SettingRow {
            iconName: "sparkles"
            title: qsTr("Sharpen (Anime4K)")
            description: App.mobile ? qsTr("Cleaner lines and upscaling for lower-resolution episodes.")
                : qsTr("Cleaner lines and upscaling for lower-resolution episodes. Best needs a strong graphics card.")
            Segmented {
                // Phones draw with OpenGL ES, where Best's networks garble.
                options: [{ value: "off", label: qsTr("Off") }, { value: "fast", label: qsTr("Fast") }]
                    .concat(App.mobile ? [] : [{ value: "hq", label: qsTr("Best") }])
                value: App.mobile && App.settings.animeSharpen === "hq" ? "fast" : App.settings.animeSharpen || "off"
                onPicked: (v) => App.setSetting("animeSharpen", v)
            }
        }
        SettingRow {
            iconName: "crop"
            title: qsTr("Fit")
            description: qsTr("How the picture fills the player. Also in the player's settings while watching.")
            Select {
                model: [{ value: "fit", label: qsTr("Fit") }, { value: "crop", label: qsTr("Crop to fill") },
                        { value: "stretch", label: qsTr("Stretch") }, { value: "original", label: qsTr("Original size") }]
                value: App.settings.playerFit || "fit"
                onPicked: (v) => App.setSetting("playerFit", v)
            }
        }
        // Android: how MediaCodec's frames reach the picture (#30).
        SettingRow {
            visible: App.platform === "android"
            iconName: "gauge"
            title: qsTr("Video decoding")
            description: qsTr("Hardware decoding with the frames copied back is steady on every phone. Direct is a little lighter but froze the picture on some phones after the next episode. Takes effect on the next video.")
            Select {
                model: [{ value: "copy", label: qsTr("Hardware (steady)") }, { value: "direct", label: qsTr("Hardware, direct") },
                        { value: "off", label: qsTr("Software") }]
                value: App.settings.playerHwdec || "copy"
                onPicked: (v) => App.setSetting("playerHwdec", v)
            }
        }
    }

    // Touch gestures on the picture (SCANCORE's player on phones).
    SettingsGroup {
        visible: Anime.nativePlayerAvailable && App.platform === "android"
        title: qsTr("Gestures")
        note: qsTr("A tap shows or hides the controls. Brightness changes only while SCANCORE is open.")
        SettingRow {
            iconName: "move-vertical"
            title: qsTr("Swipe for brightness, volume and seeking")
            description: qsTr("Up or down on the left for brightness, on the right for volume; sideways to seek.")
            Toggle { checked: App.settings.playerSwipes !== false; onToggled: App.setSetting("playerSwipes", checked) }
        }
        SettingRow {
            iconName: "rotate-ccw"
            title: qsTr("Double tap the sides to skip 10 seconds")
            description: qsTr("A double tap in the middle pauses either way.")
            Toggle { checked: App.settings.playerDoubleTapSeek !== false; onToggled: App.setSetting("playerDoubleTapSeek", checked) }
        }
        SettingRow {
            iconName: "gauge"
            title: qsTr("Hold for 2× speed")
            description: qsTr("Plays twice as fast while your finger stays down.")
            Toggle { checked: App.settings.playerHoldSpeed !== false; onToggled: App.setSetting("playerHoldSpeed", checked) }
        }
    }
}
