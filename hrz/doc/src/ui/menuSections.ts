const ACCORDION_STATE_ATTR = "hrz-accordion-state";

document.addEventListener("DOMContentLoaded", () => {
    document.querySelectorAll(".menu-section-header").forEach((section) =>
        section.addEventListener("click", function (event) {
            let section = (event.target as HTMLElement).closest(".menu-section");
            if (section.getAttribute(ACCORDION_STATE_ATTR) == "open") {
                section.setAttribute(ACCORDION_STATE_ATTR, "closed");
            } else {
                section.setAttribute(ACCORDION_STATE_ATTR, "open");
            }
        })
    );
});
