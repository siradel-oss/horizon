import { html, css, LitElement } from "lit";
import { customElement, property } from "lit/decorators.js";
import { HrzDemos } from "@siradel/horizon-doc-common";

@customElement("gallery-card")
export class GalleryCardElement extends LitElement {
    @property()
    demo: string = "";

    connectedCallback(): void {
        super.connectedCallback();

        const url = `gallery/index.html?demo=${this.demo}`;

        this.addEventListener("click", (e) => {
            e.preventDefault();
            window.open(url, "_self");
        });

        this.addEventListener("mousedown", (e) => {
            if (e.button === 1) {
                e.preventDefault();
                window.open(url, "_blank");
            }
        });
    }

    static styles = css`
        :host {
            width: 100%;
            height: 160px;
            display: flex;
            flex-direction: row;
            align-items: center;
            gap: 16px;
            border-radius: 10px;
            border: 1px solid hsla(var(--fg-color), 10%);
            overflow: clip;
            cursor: pointer;
            background: transparent;
        }

        :host(:hover) {
            background: hsla(var(--fg-color), 4%);
            box-shadow: 0 1px 4px rgba(0, 0, 0, 0.1);
        }

        img {
            aspect-ratio: 16/9;
            height: 100%;
            border-top-right-radius: 10px;
            border-bottom-right-radius: 10px;
        }

        .desc {
            color: hsla(var(--fg-color), 60%);
        }
    `;

    render() {
        return html`
            <img src="gallery/assets/thumbnails/${HrzDemos.DEFINITIONS[this.demo].thumbnailFile}" />
            <div>
                <h2 class="title">${HrzDemos.DEFINITIONS[this.demo].title}</h2>
                <p class="desc">Go to the gallery demonstration</p>
            </div>
        `;
    }
}
