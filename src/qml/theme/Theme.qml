pragma Singleton
import QtQuick
import Scancore

// Colour, type and shape tokens. The default palette follows the SCANCORE
// website: neutral greys with white (dark) or near-black (light) accents.
// The custom theme makes a whole palette from one window colour, and can add
// a background picture and a see-through window.
QtObject {
    id: theme

    readonly property string mode: App.settings.theme
    // E-ink screens (Settings → Interface): black on white whatever the theme.
    readonly property bool eink: App.settings.eink === true
    readonly property bool custom: mode === "custom" && !eink
    // Settings → Theme: {name, base, windowOpacity, panelOpacity, background,
    // backgroundAnimated, backgroundFit, backgroundDim, backgroundBlur}.
    readonly property var customTheme: App.settings.customTheme || ({})
    readonly property color customBase: hexOr(customTheme.base, "#15171d")
    readonly property bool dark: eink ? false : custom ? isDark(customBase)
        : mode === "dark" || mode === "black" || (mode === "system" && Qt.styleHints.colorScheme !== Qt.ColorScheme.Light)
    readonly property bool black: mode === "black" && !eink
    // Settings → Interface → Look: the Retro mode (tiles, panoramas and
    // pivots in the Windows Phone manner, retro/*.qml) instead of the
    // standard pages and navigation.
    readonly property bool retro: App.settings.uiMode === "retro"
    // Menus, sheets and dialogs show what is behind them, blurred
    // (GlassPanel). Not on e-ink, which cannot show it.
    readonly property bool glass: App.settings.glass === true && !eink

    // Base colours per theme. Settings draws its theme previews from these.
    readonly property var palettes: ({
        light: { bg: "#f7f7f8", sidebar: "#efeff1", surface: "#ffffff", surface2: "#f1f1f3", surface3: "#e6e6e9",
                 border: "#e2e2e6", borderStrong: "#cfcfd5", text: "#141416", textMuted: "#5f5f68", textFaint: "#94949c" },
        dark: { bg: "#0c0c0d", sidebar: "#101012", surface: "#151517", surface2: "#1c1c1f", surface3: "#26262a",
                border: "#25252a", borderStrong: "#36363c", text: "#f2f2f3", textMuted: "#a3a3ab", textFaint: "#6c6c75" },
        eink: { bg: "#ffffff", sidebar: "#ffffff", surface: "#ffffff", surface2: "#f2f2f2", surface3: "#dcdcdc",
                border: "#9a9a9a", borderStrong: "#000000", text: "#000000", textMuted: "#202020", textFaint: "#4a4a4a" },
        black: { bg: "#000000", sidebar: "#050505", surface: "#0b0b0b", surface2: "#141414", surface3: "#1d1d1d",
                 border: "#25252a", borderStrong: "#36363c", text: "#f2f2f3", textMuted: "#a3a3ab", textFaint: "#6c6c75" }
    })
    readonly property var palette: eink ? palettes.eink : custom ? generate(customBase) : palettes[black ? "black" : dark ? "dark" : "light"]

    // Surfaces over a background picture or a see-through window let it show:
    // panels by the theme's panel opacity, the page itself entirely.
    readonly property real windowOpacity: custom && Customize.translucentWindow ? clamp(Number(customTheme.windowOpacity), 0.3, 1, 1) : 1
    readonly property real panelOpacity: custom ? clamp(Number(customTheme.panelOpacity), 0.2, 1, 0.85) : 1
    readonly property string backgroundSource: custom && typeof customTheme.background === "string" ? customTheme.background : ""
    readonly property bool layered: backgroundSource.length > 0 || windowOpacity < 0.999

    // The window colour, always opaque; `bg` is the page, clear when layered.
    readonly property color base: palette.bg
    readonly property color bg: layered ? alpha(palette.bg, 0) : palette.bg
    readonly property color sidebar: layered ? alpha(palette.sidebar, panelOpacity) : palette.sidebar
    readonly property color surface: layered ? alpha(palette.surface, Math.max(0.55, panelOpacity)) : palette.surface
    readonly property color surface2: palette.surface2
    readonly property color surface3: palette.surface3
    readonly property color border: palette.border
    readonly property color borderStrong: palette.borderStrong
    readonly property color text: palette.text
    readonly property color textMuted: palette.textMuted
    readonly property color textFaint: palette.textFaint
    readonly property color overlay: dark ? Qt.rgba(0, 0, 0, 0.6) : Qt.rgba(0.1, 0.1, 0.12, 0.35)
    readonly property color danger: dark ? "#f87171" : "#dc2626"
    readonly property color success: dark ? "#4ade80" : "#16a34a"
    readonly property color warning: dark ? "#fbbf24" : "#d97706"
    // Filled rating stars (reviews): amber, black on e-ink.
    readonly property color starColor: eink ? "#000000" : dark ? "#fbbf24" : "#f59e0b"

    // Accent options; "mono" matches the website.
    readonly property var accents: ({
        mono: { dark: "#f2f2f3", light: "#141416", on: { dark: "#0c0c0d", light: "#ffffff" } },
        violet: { dark: "#a78bfa", light: "#7c3aed", on: { dark: "#120a24", light: "#ffffff" } },
        ember: { dark: "#fb923c", light: "#ea580c", on: { dark: "#1f0d02", light: "#ffffff" } },
        sky: { dark: "#38bdf8", light: "#0284c7", on: { dark: "#02131d", light: "#ffffff" } },
        rose: { dark: "#fb7185", light: "#e11d48", on: { dark: "#22050b", light: "#ffffff" } },
        emerald: { dark: "#34d399", light: "#059669", on: { dark: "#03140d", light: "#ffffff" } },
        amber: { dark: "#fbbf24", light: "#b45309", on: { dark: "#1f1402", light: "#ffffff" } },
        indigo: { dark: "#818cf8", light: "#4f46e5", on: { dark: "#0b0c24", light: "#ffffff" } }
    })
    // In the order Settings shows them.
    readonly property var accentNames: ["mono", "violet", "indigo", "sky", "emerald", "amber", "ember", "rose"]
    // Windows Phone 8's twenty accents, for the Retro mode: one colour in
    // light and dark, white on it, as on the phones' tiles.
    readonly property var retroAccents: ({
        "retro-lime": "#a4c400", "retro-green": "#60a917", "retro-emerald": "#008a00", "retro-teal": "#00aba9",
        "retro-cyan": "#1ba1e2", "retro-cobalt": "#0050ef", "retro-indigo": "#6a00ff", "retro-violet": "#aa00ff",
        "retro-pink": "#f472d0", "retro-magenta": "#d80073", "retro-crimson": "#a20025", "retro-red": "#e51400",
        "retro-orange": "#fa6800", "retro-amber": "#f0a30a", "retro-yellow": "#e3c800", "retro-brown": "#825a2c",
        "retro-olive": "#6d8764", "retro-steel": "#647687", "retro-mauve": "#76608a", "retro-taupe": "#87794e"
    })
    readonly property var retroAccentNames: ["retro-lime", "retro-green", "retro-emerald", "retro-teal", "retro-cyan",
        "retro-cobalt", "retro-indigo", "retro-violet", "retro-pink", "retro-magenta", "retro-crimson", "retro-red",
        "retro-orange", "retro-amber", "retro-yellow", "retro-brown", "retro-olive", "retro-steel", "retro-mauve", "retro-taupe"]
    readonly property string retroAccent: retroAccents[App.settings.accent] || ""
    readonly property var accentSet: retroAccent ? ({ dark: retroAccent, light: retroAccent, on: { dark: "#ffffff", light: "#ffffff" } })
        : accents[App.settings.accent] || accents.mono
    // Any colour, from the colour wheel.
    readonly property bool customAccent: App.settings.accent === "custom" && hexOr(App.settings.customAccent, "") !== ""
    readonly property color accent: eink ? "#000000" : customAccent ? App.settings.customAccent
        : retro && accentIsMono ? "#fa6800" : dark ? accentSet.dark : accentSet.light
    // Text and icons on the accent colour. (A name starting with "on" would be
    // read as a signal handler and silently never bound.)
    readonly property color accentInk: eink ? "#ffffff" : customAccent ? (luminance(accent) > 0.36 ? "#0c0c0d" : "#ffffff")
        : retro && accentIsMono ? "#ffffff" : dark ? accentSet.on.dark : accentSet.on.light
    // Retro tiles (and its accent) are a colour, never white or black: the
    // website's "mono" accent gives the phones' orange instead.
    readonly property bool accentIsMono: !customAccent && !retroAccent && (accents[App.settings.accent] || accents.mono) === accents.mono
    readonly property color tile: eink ? "#000000" : accentIsMono ? "#fa6800" : accent
    readonly property color tileInk: eink || accentIsMono || retroAccent ? "#ffffff" : accentInk

    // Novel reader page colours; "app" follows the app theme.
    readonly property var readingThemes: ({
        app: { label: qsTr("App"), bg: base, text: text, muted: textMuted, chrome: palette.surface },
        paper: { label: qsTr("Paper"), bg: "#f7f4ec", text: "#2a2925", muted: "#79766d", chrome: "#efebe1" },
        sepia: { label: qsTr("Sepia"), bg: "#efe3c8", text: "#4a3826", muted: "#8a7457", chrome: "#e6d8b9" },
        dark: { label: qsTr("Dark"), bg: "#17171a", text: "#d9d9de", muted: "#8b8b93", chrome: "#1f1f23" },
        black: { label: qsTr("Black"), bg: "#000000", text: "#c9c9ce", muted: "#77777e", chrome: "#0d0d0f" }
    })
    // A reading theme by name, "custom" being the colours chosen in the
    // reader (novelCustomBg, novelCustomText).
    function readingLook(name) {
        if (name === "custom") {
            const bg = hexOr(App.settings.novelCustomBg, "#1b1a17")
            const fg = hexOr(App.settings.novelCustomText, "#e6dfd2")
            return { label: qsTr("Custom"), bg: bg, text: fg, muted: Qt.tint(bg, alpha(fg, 0.6)), chrome: Qt.tint(bg, alpha(fg, 0.06)) }
        }
        return readingThemes[name] || readingThemes.app
    }

    // Selawik (Microsoft's open Segoe UI stand-in) in the Retro mode.
    readonly property string fontSans: retro ? "Selawik" : "DM Sans"
    readonly property string fontDisplay: retro ? "Selawik" : "Space Grotesk"
    readonly property string fontSerif: "Literata"

    // Settings → Interface → Corners: square, subtle, default or round.
    readonly property real roundness: {
        // Retro is square.
        if (retro) return 0
        const scales = { square: 0, subtle: 0.5, round: 1.6 }
        const v = App.settings.roundness
        return scales.hasOwnProperty(v) ? scales[v] : 1
    }
    readonly property int radiusSm: Math.round(6 * roundness)
    readonly property int radiusMd: Math.round(10 * roundness)
    readonly property int radiusLg: Math.round(14 * roundness)
    readonly property int radiusXl: Math.round(20 * roundness)

    // Settings → Interface → Spacing: the gap between a page's sections.
    readonly property real spacingScale: ({ compact: 0.6, spacious: 1.4 })[App.settings.pageSpacing] || 1
    readonly property int sectionGap: Math.round((compact ? 28 : 36) * spacingScale)

    // Settings → Interface → Text size scales every text token.
    readonly property real textScale: {
        const v = Number(App.settings.textScale)
        return v >= 0.8 && v <= 1.4 ? v : 1
    }
    readonly property int textXs: Math.round(11 * textScale)
    readonly property int textSm: Math.round(13 * textScale)
    readonly property int textMd: Math.round(14 * textScale)
    readonly property int textLg: Math.round(16 * textScale)
    readonly property int textXl: Math.round(20 * textScale)
    readonly property int text2xl: Math.round((compact ? 22 : 26) * textScale)
    readonly property int text3xl: Math.round((compact ? 26 : 34) * textScale)

    // Settings → Interface → Cover size: how large covers are in grids and
    // shelves.
    readonly property real coverScale: ({ small: 0.82, large: 1.22 })[App.settings.coverSize] || 1

    // Set by Main.qml: true on phones and very narrow windows.
    property bool compact: false
    readonly property bool touch: App.platform === "android"
    readonly property int gutter: compact ? 16 : 32

    // A pill's corner for its height: square in the Retro mode.
    function pill(h) { return retro ? 0 : h / 2 }

    function alpha(c, a) {
        // "#rrggbb" strings have no r, g, b until they are made colours.
        const k = typeof c === "string" ? Qt.color(c) : c
        return Qt.rgba(k.r, k.g, k.b, a)
    }

    function mix(a, b, t) {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1)
    }

    function clamp(v, low, high, fallback) {
        return isNaN(v) ? fallback : Math.min(high, Math.max(low, v))
    }

    function hexOr(value, fallback) {
        return typeof value === "string" && /^#[0-9a-fA-F]{6}$/.test(value) ? value : fallback
    }

    // Relative luminance (WCAG), 0 for black to 1 for white.
    function luminance(c) {
        const f = (v) => v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
        return 0.2126 * f(c.r) + 0.7152 * f(c.g) + 0.0722 * f(c.b)
    }

    function isDark(c) { return luminance(c) < 0.2 }

    // A full palette from one window colour: surfaces step away from it in
    // lightness (lighter on dark windows, darker on light ones), borders are
    // calmer versions, and text keeps a hint of the hue.
    function generate(base) {
        const h = Math.max(0, base.hslHue)
        const s = base.hslSaturation
        const l = base.hslLightness
        const at = (dl, ds) => Qt.hsla(h, Math.min(1, s * ds), Math.min(1, Math.max(0, l + dl)), 1)
        const darkBase = isDark(base)
        const ink = darkBase ? Qt.hsla(h, Math.min(s, 0.12), 0.95, 1) : Qt.hsla(h, Math.min(s, 0.2), 0.09, 1)
        return {
            bg: base,
            sidebar: at(darkBase ? 0.02 : -0.025, 1),
            surface: at(darkBase ? 0.045 : 0.035, 1),
            surface2: at(darkBase ? 0.075 : -0.035, 1),
            surface3: at(darkBase ? 0.12 : -0.075, 1),
            border: at(darkBase ? 0.1 : -0.09, 0.6),
            borderStrong: at(darkBase ? 0.17 : -0.16, 0.6),
            text: ink,
            textMuted: mix(ink, base, 0.32),
            textFaint: mix(ink, base, 0.55)
        }
    }
}
