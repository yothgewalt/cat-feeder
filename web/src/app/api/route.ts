import { createRedisInstance } from "@yoth.dev/libs/redis";
import { NextRequest, NextResponse } from "next/server";

export async function GET() {
    const redis = createRedisInstance();

    const scheduleCached = await redis.get("feed_schedule_hours");
    if (scheduleCached) {
        return NextResponse.json({ schedule: JSON.parse(scheduleCached) });
    }

    return NextResponse.json({ message: "Hello, world!" });
}

export async function POST(request: NextRequest) {
    const redis = createRedisInstance();

    const body = await request.json();
    await redis.set("feed_schedule_hours", JSON.stringify(body.schedule));

    return NextResponse.json({ message: "Schedule updated" });
}
