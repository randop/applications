export interface Interface {
  id: number;
  name: string;
  alias: string;
  active: boolean;
}

export interface StatsDataPoint {
  timestamp: string;
  rx: number;
  tx: number;
  rxFormatted: string;
  txFormatted: string;
}

export interface StatsResponse {
  interfaceId: number;
  interfaceName: string;
  data: StatsDataPoint[];
}

export interface ChartDataPoint {
  name: string;
  series: { name: string; value: number }[];
}

export type TimeRange = 'hourly' | 'daily' | 'monthly';
