import {
  Component,
  Input,
  OnInit,
  OnChanges,
  SimpleChanges,
  OnDestroy,
  signal,
  computed,
} from '@angular/core';
import { ChartConfiguration, ChartData } from 'chart.js';
import { VnstatService } from '../../services/vnstat.service';
import { ThemeService } from '../../services/theme.service';
import { StatsResponse, StatsDataPoint } from '../../models/vnstat.model';

@Component({
  selector: 'app-monthly-chart',
  templateUrl: './monthly-chart.component.html',
  styleUrls: ['./monthly-chart.component.scss'],
  standalone: false,
})
export class MonthlyChartComponent implements OnInit, OnChanges, OnDestroy {
  @Input() interfaceId: number | null = null;

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
            color: isDark ? '#9ca3af' : '#374151',
          },
          grid: {
            color: isDark ? '#374151' : '#e5e7eb',
          },
        },
        y: {
          ticks: {
            color: isDark ? '#9ca3af' : '#374151',
            callback: value => this.humanizeBytes(Number(value)),
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
  ) {}

  ngOnDestroy(): void {
    // No subscriptions to clean up
  }

  ngOnInit(): void {
    this.loadData();
  }

  ngOnChanges(changes: SimpleChanges): void {
    if (changes['interfaceId']) {
      this.loadData();
    }
  }

  loadData(): void {
    this.loading.set(true);
    this.error.set(null);

    this.vnstatService.getMonthlyStats(this.interfaceId ?? undefined, 12).subscribe({
      next: (responses: StatsResponse[]) => {
        this.processData(responses);
        this.loading.set(false);
      },
      error: err => {
        this.error.set('Failed to load monthly data');
        this.loading.set(false);
        console.error('Error loading monthly data:', err);
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
      return date.toLocaleDateString(undefined, { year: 'numeric', month: 'short' });
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
