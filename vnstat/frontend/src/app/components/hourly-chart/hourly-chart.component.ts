import { Component, Input, OnInit, OnChanges, SimpleChanges } from '@angular/core';
import { ChartConfiguration, ChartData } from 'chart.js';
import { VnstatService } from '../../services/vnstat.service';
import { StatsResponse, StatsDataPoint } from '../../models/vnstat.model';

type HourlyFilterType = 'last24hours' | 'today' | 'custom';

@Component({
  selector: 'app-hourly-chart',
  templateUrl: './hourly-chart.component.html',
  styleUrls: ['./hourly-chart.component.scss'],
  standalone: false
})
export class HourlyChartComponent implements OnInit, OnChanges {
  @Input() interfaceId: number | null = null;

  filterType: HourlyFilterType = 'last24hours';
  selectedDate: string = '';

  totalChartData: ChartData<'bar'> = {
    labels: [],
    datasets: []
  };

  rxChartData: ChartData<'line'> = {
    labels: [],
    datasets: []
  };

  txChartData: ChartData<'line'> = {
    labels: [],
    datasets: []
  };

  chartOptions: ChartConfiguration['options'] = {
    responsive: true,
    maintainAspectRatio: false,
    plugins: {
      legend: {
        labels: {
          color: '#9ca3af'
        }
      },
      tooltip: {
        callbacks: {
          label: (context: any) => {
            const label = context.dataset.label || '';
            const value = context.parsed?.y ?? 0;
            return `${label}: ${this.humanizeBytes(value)}`;
          }
        }
      }
    },
    scales: {
      x: {
        ticks: {
          color: '#9ca3af'
        },
        grid: {
          color: '#374151'
        }
      },
      y: {
        ticks: {
          color: '#9ca3af',
          callback: (value: any) => this.humanizeBytes(Number(value))
        },
        grid: {
          color: '#374151'
        }
      }
    }
  };

  loading = false;
  error: string | null = null;

  constructor(private vnstatService: VnstatService) { }

  ngOnInit(): void {
    this.selectedDate = this.getTodayDateString();
    this.loadData();
  }

  ngOnChanges(changes: SimpleChanges): void {
    if (changes['interfaceId']) {
      this.loadData();
    }
  }

  onFilterChange(filterType: HourlyFilterType): void {
    this.filterType = filterType;
    if (filterType === 'today') {
      this.selectedDate = this.getTodayDateString();
    }
    this.loadData();
  }

  onDateChange(date: string): void {
    this.selectedDate = date;
    if (this.filterType === 'custom') {
      this.loadData();
    }
  }

  getTodayDateString(): string {
    return new Date().toISOString().split('T')[0];
  }

  loadData(): void {
    this.loading = true;
    this.error = null;

    let startDate: string | undefined;
    let endDate: string | undefined;
    let limit: number = 24;

    if (this.filterType === 'last24hours') {
      limit = 24;
    } else if (this.filterType === 'today') {
      const today = new Date();
      const startOfDay = new Date(today);
      startOfDay.setHours(0, 0, 0, 0);
      startDate = startOfDay.toISOString();
      endDate = today.toISOString();
    } else if (this.filterType === 'custom' && this.selectedDate) {
      const date = new Date(this.selectedDate);
      const startOfDay = new Date(date);
      startOfDay.setHours(0, 0, 0, 0);
      const endOfDay = new Date(date);
      endOfDay.setHours(23, 59, 59, 999);
      startDate = startOfDay.toISOString();
      endDate = endOfDay.toISOString();
    }

    this.vnstatService.getHourlyStats(
      this.interfaceId ?? undefined,
      limit,
      startDate,
      endDate
    ).subscribe({
      next: (responses: StatsResponse[]) => {
        this.processData(responses);
        this.loading = false;
      },
      error: (err) => {
        this.error = 'Failed to load hourly data';
        this.loading = false;
        console.error('Error loading hourly data:', err);
      }
    });
  }

  private processData(responses: StatsResponse[]): void {
    if (responses.length === 0) {
      this.totalChartData = { labels: [], datasets: [] };
      this.rxChartData = { labels: [], datasets: [] };
      this.txChartData = { labels: [], datasets: [] };
      return;
    }

    const labels = responses[0].data.map((point: StatsDataPoint) => {
      const date = new Date(point.timestamp);
      return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    });

    const totalDatasets: any[] = [];
    const rxDatasets: any[] = [];
    const txDatasets: any[] = [];
    const colors = ['#4e79a7', '#f28e2b', '#59a14f', '#e15759', '#76b7b2', '#b07aa1', '#ff9da7', '#9c755f', '#bab0ac', '#499894'];

    responses.forEach((response, index) => {
      const color = colors[index % colors.length];

      totalDatasets.push({
        label: `${response.interfaceName} (Total)`,
        data: response.data.map((p: StatsDataPoint) => p.rx + p.tx),
        backgroundColor: color + '80',
        borderColor: color,
        borderWidth: 1
      });

      rxDatasets.push({
        label: `${response.interfaceName} (RX)`,
        data: response.data.map((p: StatsDataPoint) => p.rx),
        borderColor: color,
        backgroundColor: color + '40',
        fill: false,
        tension: 0.4
      });

      txDatasets.push({
        label: `${response.interfaceName} (TX)`,
        data: response.data.map((p: StatsDataPoint) => p.tx),
        borderColor: color,
        backgroundColor: color + '40',
        fill: false,
        tension: 0.4
      });
    });

    this.totalChartData = { labels, datasets: totalDatasets };
    this.rxChartData = { labels, datasets: rxDatasets };
    this.txChartData = { labels, datasets: txDatasets };
  }

  humanizeBytes(bytes: number): string {
    if (bytes === 0) return '0 B';

    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let size = bytes;
    let unitIndex = 0;

    while (size >= 1024 && unitIndex < units.length - 1) {
      size /= 1024;
      unitIndex++;
    }

    return `${size.toFixed(2)} ${units[unitIndex]}`;
  }
}
