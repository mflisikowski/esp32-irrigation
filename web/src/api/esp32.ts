import axios from "axios";
import type { Schedule, SystemInfo, Zone } from "../types";

const api = axios.create({
	baseURL: "/api",
});

export const getZones = () => api.get<Zone[]>("/zones");
export const getSchedule = () => api.get<Schedule[]>("/schedule");
export const getRain = () => api.get<{ rain: boolean }>("/rain");
export const getInfo = () => api.get<SystemInfo>("/info");

export const zoneCommand = (id: number, action: string, seconds?: number) => {
	const params = seconds ? `${action}&seconds=${seconds}` : action;
	return api.get(`/zone/${id}/command?action=${params}`);
};

export const allOff = () => api.get("/all/off");
export const saveSchedule = (schedule: Schedule[]) =>
	api.post("/save", schedule);
