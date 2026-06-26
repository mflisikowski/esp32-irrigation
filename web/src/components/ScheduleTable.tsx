import { useCallback, useState } from "react";
import type { Schedule } from "../types";

const DAYS_SHORT = ["N", "P", "W", "Ś", "C", "P", "S"];

interface ScheduleTableProps {
	schedule: Schedule[];
	onSave: (schedule: Schedule[]) => void;
}

export function ScheduleTable({ schedule, onSave }: ScheduleTableProps) {
	const [localSchedule, setLocalSchedule] = useState<Schedule[]>(() =>
		schedule.map((z) => ({ ...z })),
	);

	const updateZone = useCallback(
		(
			id: number,
			field: "enabled" | "time" | "duration",
			value: boolean | number | string,
		) => {
			setLocalSchedule((prev) =>
				prev.map((z) => {
					if (z.id !== id) return z;

					if (field === "enabled") {
						return { ...z, enabled: value as boolean };
					}

					if (field === "time") {
						const [h, m] = (value as string).split(":").map(Number);
						return { ...z, startHour: h, startMinute: m };
					}

					if (field === "duration") {
						return { ...z, duration: (value as number) * 60 };
					}

					return z;
				}),
			);
		},
		[],
	);

	return (
		<div className="schedule">
			<h2>⏱ Harmonogram</h2>
			<table>
				<thead>
					<tr>
						<th>Strefa</th>
						<th>Aktywna</th>
						<th>Start</th>
						<th>Czas (min)</th>
						<th>Dni</th>
					</tr>
				</thead>
				<tbody>
					{localSchedule.map((z) => (
						<tr key={z.id}>
							<td>
								<strong>{z.name}</strong>
							</td>
							<td>
								<input
									type="checkbox"
									checked={z.enabled}
									onChange={(e) =>
										updateZone(z.id, "enabled", e.target.checked)
									}
								/>
							</td>
							<td>
								<input
									type="time"
									value={`${String(z.startHour).padStart(2, "0")}:${String(z.startMinute).padStart(2, "0")}`}
									onChange={(e) => updateZone(z.id, "time", e.target.value)}
								/>
							</td>
							<td>
								<input
									type="number"
									min="1"
									max="120"
									value={Math.round(z.duration / 60)}
									onChange={(e) =>
										updateZone(z.id, "duration", Number(e.target.value))
									}
								/>
							</td>
							<td>
								<div className="days">
									{DAYS_SHORT.map((day, i) => (
										<button
											key={i}
											className={(z.days >> i) & 1 ? "active" : ""}
										>
											{day}
										</button>
									))}
								</div>
							</td>
						</tr>
					))}
				</tbody>
			</table>
			<br />
			<button className="btn-save" onClick={() => onSave(localSchedule)}>
				💾 ZAPISZ HARMONOGRAM
			</button>
		</div>
	);
}
