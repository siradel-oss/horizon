import { HrzProtocol } from "./hrz_protocol.js";
import Long from "long";

export class HrzProtocolHelper {
    public static readonly SCENE_VIEW_COUNT: number = Object.keys(HrzProtocol.SceneViewIndex)
        .length;

    public static readonly CAMERA_COUNT: number = Object.keys(HrzProtocol.CameraIndex).length;

    public static uint64AsNumber(value: number | Long): number {
        if (Long.isLong(value)) {
            if (
                value.greaterThan(Number.MAX_SAFE_INTEGER) ||
                value.lessThan(Number.MIN_SAFE_INTEGER)
            ) {
                console.error(
                    `Converting Long value ${value.toString()} to number may result in precision loss.`
                );
            }
            return value.toNumber();
        }
        return value;
    }

    public static attributeAsNumber(value: HrzProtocol.IAttributeValue): number {
        if (value.numberValue !== undefined && value.numberValue !== null) {
            return value.numberValue;
        } else if (value.uint64Value !== undefined && value.uint64Value !== null) {
            return this.uint64AsNumber(value.uint64Value);
        } else if (value.int64Value !== undefined && value.int64Value !== null) {
            return this.uint64AsNumber(value.int64Value);
        } else if (value.stringValue !== undefined && value.stringValue !== null) {
            return Number.parseFloat(value.stringValue);
        } else if (value.booleanValue !== undefined && value.booleanValue !== null) {
            return value.booleanValue ? 1 : 0;
        } else {
            return 0;
        }
    }

    public static attributeAsAny(
        value: HrzProtocol.IAttributeValue
    ): number | Long | string | boolean | null {
        if (value.numberValue !== undefined && value.numberValue !== null) {
            return value.numberValue;
        } else if (value.uint64Value !== undefined && value.uint64Value !== null) {
            return value.uint64Value;
        } else if (value.int64Value !== undefined && value.int64Value !== null) {
            return value.int64Value;
        } else if (value.stringValue !== undefined && value.stringValue !== null) {
            return value.stringValue;
        } else if (value.booleanValue !== undefined && value.booleanValue !== null) {
            return value.booleanValue;
        } else {
            return null;
        }
    }
}
