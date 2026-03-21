import { Component, Input, OnInit, OnChanges, SimpleChanges } from '@angular/core';
import { CommonModule } from '@angular/common';
import { ChartConfiguration, ChartData } from 'chart.js';
import { BaseChartDirective } from 'ng2-charts';
import { VnstatService } from '../../services/vnstat.service';
import { StatsResponse, StatsDataPoint } from '../../models/vnstat.model';

@Component({
  selector: 'app-total-chart',
  templateUrl: './total-chart.component.html',
  styleUrls: ['./total-chart.component.scss'],
  standalone: true,
  imports: [CommonModule, BaseChartDirective],
})
export class TotalChartComponent implements OnInit, OnChanges {
  @Input() interfaceId: number | null = null;
  @Input() type: 'hourly' | 'daily' | 'monthly' = 'hourly';

  totalChartData: ChartData<'bar'> = {
    labels: [],
    datasets: [],
  };

  chartOptions: ChartConfiguration['options'] = {
    responsive: true,
    maintainAspectRatio: false,
    plugins: {
      legend: {
        labels: {
          color: '#9ca3af',
        },
      },
      tooltip: {
        callbacks: {
          label: context => {
            const label = context.dataset.label || '';
            const value = context.parsed.y ?? 0;
            return `${label}: ${this.humanizeBytes(value)}`;
          },
        },
      },
    },
    scales: {
      x: {
        ticks: {
          color: '#9ca3af',
        },
        grid: {
          color: '#374151',
        },
      },
      y: {
        ticks: {
          color: '#9ca3af',
          callback: value => this.humanizeBytes(Number(value)),
        },
        grid: {
          color: '#374151',
        },
      },
    },
  };

  loading = false;
  error: string | null = null;

  constructor(private vnstatService: VnstatService) {}

  ngOnInit(): void {
    this.loadData();
  }

  ngOnChanges(changes: SimpleChanges): void {
    if (changes['interfaceId'] || changes['type']) {
      this.loadData();
    }
  }

  loadData(): void {
    this.loading = true;
    this.error = null;

    const limit = this.type === 'hourly' ? 24 : this.type === 'daily' ? 30 : 12;

    let request;
    switch (this.type) {
      case 'hourly':
        request = this.vnstatService.getHourlyStats(this.interfaceId ?? undefined, limit);
        break;
      case 'daily':
        request = this.vnstatService.getDailyStats(this.interfaceId ?? undefined, limit);
        break;
      case 'monthly':
        request = this.vnstatService.getMonthlyStats(this.interfaceId ?? undefined, limit);
        break;
    }

    request!.subscribe({
      next: (responses: StatsResponse[]) => {
        this.processData(responses);
        this.loading = false;
      },
      error: err => {
        this.error = `Failed to load ${this.type} total data`;
        this.loading = false;
        console.error(`Error loading ${this.type} total data:`, err);
      },
    });
  }

  private processData(responses: StatsResponse[]): void {
    if (responses.length === 0) {
      this.totalChartData = { labels: [], datasets: [] };
      return;
    }

    const labels = responses[0].data.map((point: StatsDataPoint) => {
      const date = new Date(point.timestamp);
      if (this.type === 'hourly') {
        return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
      } else if (this.type === 'monthly') {
        return date.toLocaleDateString(undefined, { year: 'numeric', month: 'short' });
      }
      return date.toLocaleDateString();
    });

    const datasets: any[] = [];
    const colors = [
      '#4e79a7',
      '#f28e2b',
      '#59a14f',
      '#e15759',
      '#76b7b2',
      '#b07aa1',
      '#ff9da7',
      '#9c755f',
      '#bab0ac',
      '#499894',
    ];

    responses.forEach((response, index) => {
      const color = colors[index % colors.length];

      datasets.push({
        label: `${response.interfaceName} (Total)`,
        data: response.data.map((p: StatsDataPoint) => p.rx + p.tx),
        backgroundColor: color + '80',
        borderColor: color,
        borderWidth: 1,
      });
    });

    this.totalChartData = { labels, datasets };
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
