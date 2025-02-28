import Fuse, { IFuseOptions } from "fuse.js";
import { HrzDemos } from "@siradel/horizon-doc-common";

interface SearchItem {
    page: string;
    title: string;
    tags: string[];
}

let SEARCH_INDEX: SearchItem[] = [];

for (const demo in HrzDemos.DEFINITIONS) {
    SEARCH_INDEX.push({
        page: `gallery/index.html?demo=${demo}`,
        title: "Demo > " + HrzDemos.DEFINITIONS[demo].title,
        tags: HrzDemos.DEFINITIONS[demo].tags,
    });
}

(window as any).addSearchItem = function (page: string, title: string) {
    SEARCH_INDEX.push({ page, title, tags: [] });
};

let fuse: Fuse<SearchItem> | null = null;
function initFuse() {
    const options: IFuseOptions<SearchItem> = {
        keys: [
            {
                name: "title",
                weight: 0.75,
            },
            {
                name: "tags",
                weight: 0.25,
            },
        ],
        minMatchCharLength: 3,
        threshold: 0.4,
    };
    fuse = new Fuse(SEARCH_INDEX, options);
}

document.addEventListener("DOMContentLoaded", function () {
    let searchInput = document.getElementById("search-input") as HTMLInputElement;
    let searchResult = document.getElementById("search-result");

    searchInput.addEventListener("input", function () {
        if (!fuse) {
            initFuse();
        }

        let value = searchInput.value;
        if (value && value.length > 2) {
            let result = fuse.search(value);
            if (result.length > 0) {
                let links = result
                    .map((res) => {
                        let a = document.createElement("a");
                        a.href = res.item.page;
                        a.innerText = res.item.title;
                        return a;
                    })
                    .map((a) => {
                        let li = document.createElement("li");
                        li.appendChild(a);
                        return li;
                    });

                let ul = document.createElement("ul");
                ul.append(...links);

                searchResult.innerHTML = "";
                searchResult.appendChild(ul);
                searchResult.style.display = "block";
            } else {
                searchResult.style.display = "none";
            }
        } else {
            searchResult.style.display = "none";
        }
    });
});
