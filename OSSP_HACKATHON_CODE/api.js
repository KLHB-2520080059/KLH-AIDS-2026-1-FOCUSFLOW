// Small helper for talking to the backend and managing the login session.
const API_BASE = '/api';

function getToken() {
  return localStorage.getItem('cpu_token');
}

function setSession(token, user) {
  localStorage.setItem('cpu_token', token);
  localStorage.setItem('cpu_user', JSON.stringify(user));
}

function clearSession() {
  localStorage.removeItem('cpu_token');
  localStorage.removeItem('cpu_user');
}

async function api(path, options = {}) {
  const headers = { 'Content-Type': 'application/json' };
  const token = getToken();
  if (token) headers.Authorization = 'Bearer ' + token;

  const res = await fetch(API_BASE + path, {
    method: options.method || 'GET',
    headers,
    body: options.body ? JSON.stringify(options.body) : undefined
  });

  let data = {};
  try { data = await res.json(); } catch { /* empty response */ }

  if (!res.ok) throw new Error(data.error || 'Something went wrong. Try again.');
  return data;
}
