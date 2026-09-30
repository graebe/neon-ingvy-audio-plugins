/*
 * Copy, the way a plugin's WebView allows it. Copyright (c) 2026 Torben Gräber. MIT.
 *
 * navigator.clipboard.writeText needs a secure context and a user gesture the
 * WKWebView inside a plugin does not reliably grant, so the textarea and
 * execCommand('copy') route is the fallback. PASTE HAS NO EQUIVALENT:
 * readText() is behind a permission prompt the WebView cannot show, so paste
 * is a field the user pastes INTO (see SettingsRow).
 */
export async function copyToClipboard(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch { /* fall through */ }
  try {
    const ta = document.createElement('textarea');
    ta.value = text;
    /* Off-screen but NOT display:none -- the selection has to be real. */
    ta.setAttribute('readonly', '');
    ta.style.cssText = 'position:fixed;top:0;left:-9999px;opacity:0';
    document.body.appendChild(ta);
    ta.select();
    ta.setSelectionRange(0, ta.value.length);
    const ok = document.execCommand('copy');
    ta.remove();
    return ok;
  } catch { return false; }
}

/**
 * The paste shortcut's modifier as the hint should name it: ⌘ on Apple
 * platforms, Ctrl- elsewhere. navigator.platform is deprecated;
 * userAgentData is where it lives now, and the user agent string is the
 * fallback every WebView still has.
 */
export function modKey(nav = globalThis.navigator) {
  const platform = nav?.userAgentData?.platform ?? nav?.userAgent ?? '';
  return /mac|iphone|ipad|ios/i.test(platform) ? '⌘' : 'Ctrl-';
}
