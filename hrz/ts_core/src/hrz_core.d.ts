declare function HrzCore(Module: any): HrzCore.Promise;

declare namespace HrzCore {
    export interface Buffer {
        size: number;
        data: number;
    }

    export interface NativeApi {
        HEAPU8: Uint8Array;

        _malloc(size: number): number;
        _free(ptr: number): void;

        hrz_init(args: Buffer, canvasSelector: string): number;
        hrz_rpc(service: number, method: number, input: Buffer): Buffer;
        hrz_free_rpc(ptr: number): void;
    }

    export interface Promise {
        then(cb: { (api: NativeApi): void }): void;
    }
}

export default HrzCore;
