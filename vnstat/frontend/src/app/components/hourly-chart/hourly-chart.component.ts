import {
  Component,
  Input,
  OnInit,
  OnChanges,
  SimpleChanges,
  OnDestroy,
  signal,
  computed,
  effect,
} from '@angular/core';
import { CommonModule } from '@angular/common';
import { ChartConfiguration, ChartData } from 'chart.js';
import { BaseChartDirective } from 'ng2-charts';
import { VnstatService } from '../../services/vnstat.service';
import { ThemeService } from '../../services/theme.service';
import { StatsResponse, StatsDataPoint } from '../../models/vnstat.model';

type HourlyFilterType = 'last24hours' | 'today' | 'custom';

@Component({
  selector: 'app-hourly-chart',
  templateUrl: './hourly-chart.component.html',
  styleUrls: ['./hourly-chart.component.scss'],
  standalone: true,
  imports: [CommonModule, BaseChartDirective],
})
export class HourlyChartComponent implements OnInit, OnChanges, OnDestroy {
  @Input() interfaceId: number | null = null;

  filterType = signal<HourlyFilterType>('last24hours');
  selectedDate = signal<string>('');
  loading = signal<boolean>(false);
  error = signal<string | null>(null);

  private totalChartDataRaw = signal<ChartData<'bar'>>({ labels: [], datasets: [] });
  private rxChartDataRaw = signal<ChartData<'line'>>({ labels: [], datasets: [] });
  private txChartDataRaw = signal<ChartData<'line'>>({ labels: [], datasets: [] });

  chartOptions = computed<ChartConfiguration['options']>(() => {
    const theme = this.themeService.currentTheme();
    const isDark = this.themeService.isDark();

    return {
      responsive: true,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          labels: {
            color: isDark ? '#9ca3af' : '#374151',
          },
        },
        tooltip: {
          callbacks: {
            label: (context: any) => {
              const label = context.dataset.label || '';
              const value = context.parsed?.y ?? 0;
              return `${label}: ${this.humanizeBytes(value)}`;
            },
          },
        },
      },
      scales: {
        x: {
          ticks: {
            color: isDark ? '#9ca3af' : '#374151',
          },
          grid: {
            color: isDark ? '#374151' : '#e5e7eb',
          },
        },
        y: {
          ticks: {
            color: isDark ? '#9ca3af' : '#374151',
            callback: (value: any) => this.humanizeBytes(Number(value)),
          },
          grid: {
            color: isDark ? '#374151' : '#e5e7eb',
          },
        },
      },
    };
  });

  // Expose chart data for template
  totalChartData = computed(() => this.totalChartDataRaw());
  rxChartData = computed(() => this.rxChartDataRaw());
  txChartData = computed(() => this.txChartDataRaw());

  constructor(
    private vnstatService: VnstatService,
    private themeService: ThemeService
  ) {
    // Effect to reload data when filter type or selected date changes
    effect(() => {
      this.filterType();
      this.selectedDate();
      this.loadData();
    });
  }

  ngOnDestroy(): void {
    // No subscriptions to clean up
  }

  ngOnInit(): void {
    this.selectedDate.set(this.getTodayDateString());
    this.loadData();
  }

  ngOnChanges(changes: SimpleChanges): void {
    if (changes['interfaceId']) {
      this.loadData();
    }
  }

  onFilterChange(filterType: HourlyFilterType): void {
    this.filterType.set(filterType);
    if (filterType === 'today') {
      this.selectedDate.set(this.getTodayDateString());
    }
  }

  onDateChange(date: string): void {
    this.selectedDate.set(date);
  }

  getTodayDateString(): string {
    return new Date().toISOString().split('T')[0];
  }

  loadData(): void {
    this.loading.set(true);
    this.error.set(null);

    let startDate: string | undefined;
    let endDate: string | undefined;
    let limit: number = 24;

    const currentFilterType = this.filterType();
    const currentSelectedDate = this.selectedDate();

    if (currentFilterType === 'last24hours') {
      limit = 24;
    } else if (currentFilterType === 'today') {
      const today = new Date();
      const startOfDay = new Date(today);
      startOfDay.setHours(0, 0, 0, 0);
      startDate = startOfDay.toISOString();
      endDate = today.toISOString();
    } else if (currentFilterType === 'custom' && currentSelectedDate) {
      const date = new Date(currentSelectedDate);
      const startOfDay = new Date(date);
      startOfDay.setHours(0, 0, 0, 0);
      const endOfDay = new Date(date);
      endOfDay.setHours(23, 59, 59, 999);
      startDate = startOfDay.toISOString();
      endDate = endOfDay.toISOString();
    }

    this.vnstatService
      .getHourlyStats(this.interfaceId ?? undefined, limit, startDate, endDate)
      .subscribe({
        next: (responses: StatsResponse[]) => {
          this.processData(responses);
          this.loading.set(false);
        },
        error: err => {
          this.error.set('Failed to load hourly data');
          this.loading.set(false);
          console.error('Error loading hourly data:', err);
        },
      });
  }

  private processData(responses: StatsResponse[]): void {
    if (responses.length === 0) {
      this.totalChartDataRaw.set({ labels: [], datasets: [] });
      this.rxChartDataRaw.set({ labels: [], datasets: [] });
      this.txChartDataRaw.set({ labels: [], datasets: [] });
      return;
    }

    const labels = responses[0].data.map((point: StatsDataPoint) => {
      const date = new Date(point.timestamp);
      return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    });

    const totalDatasets: any[] = [];
    const rxDatasets: any[] = [];
    const txDatasets: any[] = [];
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

      totalDatasets.push({
        label: `${response.interfaceName} (Total)`,
        data: response.data.map((p: StatsDataPoint) => p.rx + p.tx),
        backgroundColor: color + '80',
        borderColor: color,
        borderWidth: 1,
      });

      rxDatasets.push({
        label: `${response.interfaceName} (RX)`,
        data: response.data.map((p: StatsDataPoint) => p.rx),
        borderColor: color,
        backgroundColor: color + '40',
        fill: false,
        tension: 0.4,
      });

      txDatasets.push({
        label: `${response.interfaceName} (TX)`,
        data: response.data.map((p: StatsDataPoint) => p.tx),
        borderColor: color,
        backgroundColor: color + '40',
        fill: false,
        tension: 0.4,
      });
    });

    this.totalChartDataRaw.set({ labels, datasets: totalDatasets });
    this.rxChartDataRaw.set({ labels, datasets: rxDatasets });
    this.txChartDataRaw.set({ labels, datasets: txDatasets });
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
