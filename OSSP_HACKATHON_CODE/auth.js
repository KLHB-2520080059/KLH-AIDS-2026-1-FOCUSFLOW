// Handles the login and signup forms.
document.addEventListener('DOMContentLoaded', () => {
  const form = document.getElementById('login-form') || document.getElementById('signup-form');
  if (!form) return;

  // Already logged in? Go straight to the dashboard.
  if (getToken()) {
    location.href = 'dashboard.html';
    return;
  }

  const errorBox = document.getElementById('form-error');
  const button = form.querySelector('button[type="submit"]');
  const isSignup = form.id === 'signup-form';

  form.addEventListener('submit', async event => {
    event.preventDefault();
    errorBox.hidden = true;

    const data = Object.fromEntries(new FormData(form));
    if (!form.checkValidity()) {
      errorBox.textContent = 'Fill in every field to continue.';
      errorBox.hidden = false;
      return;
    }

    button.disabled = true;
    button.textContent = isSignup ? 'Creating account...' : 'Logging in...';
    try {
      const res = await api(isSignup ? '/auth/signup' : '/auth/login', { method: 'POST', body: data });
      setSession(res.token, res.user);
      location.href = 'dashboard.html';
    } catch (error) {
      errorBox.textContent = error.message;
      errorBox.hidden = false;
      button.disabled = false;
      button.textContent = isSignup ? 'Create account' : 'Log in';
    }
  });
});
