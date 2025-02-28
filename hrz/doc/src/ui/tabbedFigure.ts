export function initTabbedFigures() {
    const tabbedFigures = document.getElementsByClassName("tabbed-figure");
    for (let tabbedFigure of Array.from(tabbedFigures)) {
        if (!(tabbedFigure instanceof HTMLElement)) continue;

        const header = tabbedFigure.querySelector(".tabbed-figure-header") as HTMLElement;
        const headerEntries = header.children;

        const contents = tabbedFigure.querySelector(".tabbed-figure-contents") as HTMLElement;
        const contentsEntries = contents.children;

        for (let i = 0; i < Math.max(headerEntries.length, contentsEntries.length); i++) {
            const headerEntry = headerEntries[i] as HTMLElement;

            headerEntry.onclick = () => {
                for (let j = 0; j < headerEntries.length; j++) {
                    const h = headerEntries[j] as HTMLElement;
                    const c = contentsEntries[j] as HTMLElement;

                    if (h.isEqualNode(headerEntry)) {
                        h.classList.add("active");
                        c.style.display = "";
                    } else {
                        h.classList.remove("active");
                        c.style.display = "none";
                    }
                }
            };

            if (i === 0) {
                headerEntry.classList.add("active");
            } else {
                headerEntry.classList.remove("active");
            }
        }

        for (let i = 0; i < Math.max(contentsEntries.length, contentsEntries.length); i++) {
            let contentsEntry = contentsEntries[i] as HTMLElement;
            if (i !== 0) {
                contentsEntry.style.display = "none";
            }
        }
    }
}

document.addEventListener("DOMContentLoaded", initTabbedFigures);
