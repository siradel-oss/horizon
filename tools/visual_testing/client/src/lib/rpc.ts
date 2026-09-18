// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

import type { ErrorResponse, MethodMap } from "@/proto/schema";

export class RpcError extends Error {
    constructor(
        public status: number,
        message: string
    ) {
        super(message);
        this.name = "RpcError";
    }
}

export async function callRpc<M extends keyof MethodMap>(
    method: M,
    request: MethodMap[M][0]
): Promise<MethodMap[M][1]> {
    const res = await fetch(`/rpc/${method}`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(request),
    });

    if (!res.ok) {
        let message = res.statusText;
        try {
            const body = (await res.json()) as ErrorResponse;
            if (body?.message) message = body.message;
        } catch {
            // Response body wasn't JSON; fall back to the status text.
        }
        throw new RpcError(res.status, message);
    }

    return (await res.json()) as MethodMap[M][1];
}

export async function fileToBase64(file: File): Promise<string> {
    const buffer = await file.arrayBuffer();
    let binary = "";
    const bytes = new Uint8Array(buffer);
    for (let i = 0; i < bytes.length; i++) {
        binary += String.fromCharCode(bytes[i]);
    }
    return btoa(binary);
}
