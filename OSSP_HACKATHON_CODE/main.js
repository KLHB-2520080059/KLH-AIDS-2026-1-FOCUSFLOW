// Shared behavior for every page: nav state, logout, and a tiny element builder.

// Create an element safely. Text is always inserted as text, never as HTML.
function h(tag, attrs = {}, children = []) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === undefined || value === null || value === false) continue;
    if (key === 'text') el.textContent = value;
    else if (key.startsWith('on') && typeof value === 'function') el.addEventListener(key.slice(2), value);
    else el.setAttribute(key, value === true ? '' : value);
  }
  [].concat(children)
    .filter(c => c !== undefined && c !== null && c !== false)
    .forEach(c => el.append(c instanceof Node ? c : document.createTextNode(String(c))));
  return el;
}

function updateNav() {
  const loggedIn = !!getToken();
  document.querySelectorAll('[data-auth="out"]').forEach(el => { el.hidden = loggedIn; });
  document.querySelectorAll('[data-auth="in"]').forEach(el => { el.hidden = !loggedIn; });
}

document.addEventListener('click', event => {
  if (event.target.closest('[data-logout]')) {
    clearSession();
    location.href = 'index.html';
  }
});

document.addEventListener('DOMContentLoaded', updateNav);
