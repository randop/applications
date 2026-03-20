export class StatsDataPointDto {
  timestamp: string;
  rx: number;
  tx: number;
  rxFormatted: string;
  txFormatted: string;
}

export class StatsResponseDto {
  interfaceId: number;
  interfaceName: string;
  data: StatsDataPointDto[];
}

export class InterfacesResponseDto {
  id: number;
  name: string;
  alias: string;
  active: boolean;
}
