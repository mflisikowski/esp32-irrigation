import type { Zone } from "../types";

interface ZoneCardProps {
	zone: Zone;
	onCommand: (id: number, action: string) => void;
	onRun: (id: number) => void;
}

export function ZoneCard({ zone, onCommand, onRun }: ZoneCardProps) {
	return (
		<div className={`card ${zone.active ? "active" : ""}`}>
			<h3>
				{zone.name}
				<span className={`status ${zone.active ? "on" : "off"}`}></span>
			</h3>
			<div className="timer">
				{zone.active
					? `⏳ ${Math.floor(zone.remaining / 60)}:${(zone.remaining % 60).toString().padStart(2, "0")}`
					: "⏸ gotowy"}
			</div>
			<div className="btn-group">
				<button className="btn btn-on" onClick={() => onCommand(zone.id, "on")}>
					▶ WŁ
				</button>
				<button
					className="btn btn-off"
					onClick={() => onCommand(zone.id, "off")}
				>
					■ WYŁ
				</button>
				<button className="btn btn-run" onClick={() => onRun(zone.id)}>
					⏱ 5 min
				</button>
			</div>
		</div>
	);
}
