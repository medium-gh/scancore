pragma Singleton
import QtQuick

// The small page web players load around a provider's player (an iframe):
// it reads the player's time through getTime/seek messages, so SCANCORE can
// resume, skip intros and play the next episode. Taps go to the player
// itself, its own controls and menus; nothing of ours lies over it.
// Pop-ups are refused by the web views (WebPlayer, WebPlayer.java), and if
// the player's frame is navigated away after playback started (an ad), it is
// put back and seeks to where it was.
// Reports go to window.__sc, and with `titled` also to the document title
// (Android's web view, see WebPlayerSystem).
QtObject {
    function wrapper(src, titled) {
        const safe = String(src).replace(/"/g, "&quot;").replace(/</g, "&lt;")
        return "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            + "<style>html,body{margin:0;height:100%;background:#000;overflow:hidden}iframe{border:0;width:100%;height:100%}</style>"
            + "</head><body><iframe id='f' src=\"" + safe + "\" allow='autoplay; fullscreen; encrypted-media; picture-in-picture' allowfullscreen referrerpolicy='origin'></iframe>"
            + "<script>(function(){"
            + "var S=window.__sc={t:0,d:0,s:'',h264:!!(window.MediaSource&&MediaSource.isTypeSupported('video/mp4; codecs=\"avc1.42E01E\"'))};"
            + "var f=document.getElementById('f'),src=f.src,titled=" + (titled ? "true" : "false") + ",last=-1,started=false,loads=0,restoring=false;"
            + "function pub(){if(!titled)return;var v='sc:'+JSON.stringify(S);if(document.title!==v)document.title=v}"
            + "function post(m){try{f.contentWindow.postMessage(m,'*')}catch(_){}}"
            + "addEventListener('message',function(e){if(e.source!==f.contentWindow)return;var x=e.data;"
            + "if(typeof x==='string'){try{x=JSON.parse(x)}catch(_){return}}if(!x)return;"
            + "if(x.currentTime!==undefined){var t=+x.currentTime||0;S.p=last>=0&&t>last;if(S.p)started=true;last=t;S.t=t;S.d=+x.duration||0}"
            + "if(x.playerStatus)S.s=String(x.playerStatus);pub()});"
            + "setInterval(function(){post({command:'getTime'})},1000);"
            + "window.scSeek=function(v){post({command:'seek',value:v})};"
            // A TV's remote, through the player's own message API (play,
            // pause, seek): play or pause, and a jump from where it is.
            + "window.scToggle=function(){post({command:S.p?'pause':'play'});S.p=!S.p};"
            + "window.scSeekBy=function(d){var v=Math.max(0,S.t+d);if(S.d>0)v=Math.min(S.d-1,v);S.t=v;last=-1;post({command:'seek',value:v})};"
            // The frame went somewhere else after playback started: back.
            + "f.addEventListener('load',function(){loads++;if(restoring){restoring=false;return}"
            + "if(loads>1&&started){var at=S.t;restoring=true;f.src=src;setTimeout(function(){window.scSeek(at)},2500)}});"
            + "})()</script></body></html>"
    }
}
