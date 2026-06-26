import type { Zone } from "../types";
import { ZoneCard } from "./ZoneCard";

interface ZoneGridProps {
	zones: Zone[];
	onCommand: (id: number, action: string) => void;
	onRun: (id: number) => void;
}

export function ZoneGrid({ zones, onCommand, onRun }: ZoneGridProps) {
	return (
		<div className="grid">
			{zones.map((zone) => (
				<ZoneCard
					key={zone.id}
					zone={zone}
					onCommand={onCommand}
					onRun={onRun}
				/>
			))}
		</div>
	);
}
