import { HrzProtocol } from "@siradel/horizon-protocol";

export class Vector3 {
    public x: number;
    public y: number;
    public z: number;

    public constructor(x: number, y: number, z: number) {
        this.x = x;
        this.y = y;
        this.z = z;
    }
}

export function vec3Add(u: Vector3, v: Vector3): Vector3 {
    return new Vector3(u.x + v.x, u.y + v.y, u.z + v.z);
}

export function vec3MulScalar(v: Vector3, scalar: number): Vector3 {
    return new Vector3(v.x * scalar, v.y * scalar, v.z * scalar);
}

export function length2(v: Vector3): number {
    return dot(v, v);
}

export function cross(a: Vector3, b: Vector3): Vector3 {
    return new Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

export function dot(a: Vector3, b: Vector3): number {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

export function normalize(v: Vector3): Vector3 {
    let inv_length = 1.0 / Math.sqrt(length2(v));
    return new Vector3(v.x * inv_length, v.y * inv_length, v.z * inv_length);
}

export class Quaternion {
    public x: number;
    public y: number;
    public z: number;
    public w: number;

    public constructor(x: number = 0, y: number = 0, z: number = 0, w: number = 1) {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    public toEulerAngles() {
        let sinr_cosp = 2 * (this.w * this.x + this.y * this.z);
        let cosr_cosp = 1 - 2 * (this.x * this.x + this.y * this.y);
        let sinp = Math.sqrt(1 + 2 * (this.w * this.y - this.x * this.z));
        let cosp = Math.sqrt(1 - 2 * (this.w * this.y - this.x * this.z));
        let siny_cosp = 2 * (this.w * this.z + this.x * this.y);
        let cosy_cosp = 1 - 2 * (this.y * this.y + this.z * this.z);

        return {
            yaw: Math.atan2(siny_cosp, cosy_cosp),
            pitch: 2 * Math.atan2(sinp, cosp) - Math.PI / 2,
            roll: Math.atan2(sinr_cosp, cosr_cosp),
        };
    }
}

export function toQuat(proto: HrzProtocol.IQuat): Quaternion {
    return new Quaternion(proto.x, proto.y, proto.z, proto.w);
}

export function quatFromEulerAngles(
    yaw: number = 0,
    pitch: number = 0,
    roll: number = 0
): Quaternion {
    let cr = Math.cos(roll * 0.5);
    let sr = Math.sin(roll * 0.5);
    let cp = Math.cos(pitch * 0.5);
    let sp = Math.sin(pitch * 0.5);
    let cy = Math.cos(yaw * 0.5);
    let sy = Math.sin(yaw * 0.5);

    let qx = sr * cp * cy - cr * sp * sy;
    let qy = cr * sp * cy + sr * cp * sy;
    let qz = cr * cp * sy - sr * sp * cy;
    let qw = cr * cp * cy + sr * sp * sy;

    return new Quaternion(qx, qy, qz, qw);
}

export function vec3Rotate(q: Quaternion, v: Vector3): Vector3 {
    let axis = new Vector3(q.x, q.y, q.z);
    if (length2(axis) < 1e-6) return v;

    let v1 = vec3MulScalar(v, q.w * q.w - length2(axis));
    let v2 = vec3MulScalar(cross(axis, v), 2 * q.w);
    let v3 = vec3MulScalar(axis, 2 * dot(axis, v));
    return vec3Add(vec3Add(v1, v2), v3);
}

export function toRadians(angle: number): number {
    return (angle * Math.PI) / 180;
}

export function toDegrees(angle: number): number {
    return (angle * 180) / Math.PI;
}
