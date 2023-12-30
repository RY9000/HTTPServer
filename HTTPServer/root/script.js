document.addEventListener('DOMContentLoaded', function () {
    const passwordInput = document.getElementById('password');
    const confirmPasswordInput = document.getElementById('confirmPassword');

    function checkPasswordMatch() {
        if (passwordInput && confirmPasswordInput) {
            const password = passwordInput.value;
            const confirmPassword = confirmPasswordInput.value;

            if (password !== confirmPassword) {
                confirmPasswordInput.setCustomValidity('Passwords do not match');
            }
            else {
                confirmPasswordInput.setCustomValidity('');
            }
        }
    }
    if (passwordInput) {
        passwordInput.addEventListener('input', checkPasswordMatch);
    }
    if (confirmPasswordInput) {
        confirmPasswordInput.addEventListener('input', checkPasswordMatch);
    }
});

async function submitForm(event, apiUrl, successRedirect) {
    event.preventDefault();

    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;

    try {
        const response = await fetch(apiUrl, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, password }),
        });

        const errorMessage = document.getElementById('error-message');
        const successMessage = document.getElementById('success-message');
        if (errorMessage) {
            errorMessage.textContent = '';
        }
        if (successMessage) {
            successMessage.textContent = '';
        }

        if (response.status === 200) {
            window.location.href = successRedirect;
        }
        else if (response.status === 201) {
            successMessage.textContent = 'Register successfully';
        }
        else if (response.status === 400) {
            errorMessage.textContent = 'Error: Bad request';
        }
        else if (response.status === 401) {
            errorMessage.textContent = 'Error: Invalid username or password';
        }
        else if (response.status === 409) {
            errorMessage.textContent = 'Error: Username has been taken';
        }
        else {
            errorMessage.textContent = 'An error occurred';
        }
    }
    catch (error) {
        console.error('Error during form submission:', error);
    }
}

function getCookie(name) {
    const cookies = document.cookie.split(';');
    for (const cookie of cookies) {
        const [cookieName, cookieValue] = cookie.trim().split('=');
        if (cookieName === name) {
            const attributes = cookieValue.split(' ');
            const filteredAttributes = attributes.filter(attr => attr.toLowerCase() !== 'secure=false');
            const correctedCookieValue = filteredAttributes.join(' ');
            return correctedCookieValue;
        }
    }
    return null;
}