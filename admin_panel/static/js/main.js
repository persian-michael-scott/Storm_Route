
document.addEventListener('DOMContentLoaded', function () {

    // --- Real-time User Search --- //
    const searchInput = document.getElementById('search');
    const userTableBody = document.querySelector('table tbody');
    const allRows = userTableBody ? Array.from(userTableBody.querySelectorAll('tr')) : [];
    const userCountHeader = document.querySelector('h2');

    if (searchInput) {
        searchInput.addEventListener('keyup', function (e) {
            const searchTerm = e.target.value.toLowerCase();
            let visibleCount = 0;

            allRows.forEach(row => {
                const uuidCell = row.querySelector('code');
                const descriptionCell = row.cells[0];
                if (uuidCell && descriptionCell) {
                    const uuid = uuidCell.textContent.toLowerCase();
                    const description = descriptionCell.textContent.toLowerCase();
                    if (uuid.includes(searchTerm) || description.includes(searchTerm)) {
                        row.style.display = '';
                        visibleCount++;
                    } else {
                        row.style.display = 'none';
                    }
                }
            });
            userCountHeader.textContent = `${visibleCount} user(s) found.`;
        });
    }

    // --- Copy to Clipboard --- //
    document.querySelectorAll('.copy-btn').forEach(button => {
        button.addEventListener('click', function () {
            const uuid = this.dataset.uuid;
            navigator.clipboard.writeText(uuid).then(() => {
                this.textContent = 'Copied!';
                setTimeout(() => { this.textContent = 'Copy'; }, 2000);
            }).catch(err => {
                console.error('Failed to copy: ', err);
            });
        });
    });

    // --- Dismissible Flash Messages (Toasts) --- //
    const flashMessages = document.querySelectorAll('.flash-message');
    flashMessages.forEach((flash, index) => {
        const toast = document.createElement('div');
        toast.className = `toast ${flash.dataset.category}`;
        toast.textContent = flash.textContent;
        document.body.appendChild(toast);

        // Show toast
        setTimeout(() => {
            toast.classList.add('show');
        }, 100 * index);

        // Hide after 5 seconds
        setTimeout(() => {
            toast.classList.remove('show');
            setTimeout(() => toast.remove(), 500);
        }, 5000 + (100 * index));
    });
});
