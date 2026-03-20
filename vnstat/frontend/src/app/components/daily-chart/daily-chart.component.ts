import {
  Component,
  Input,
  OnInit,
  OnChanges,
  SimpleChanges,
  OnDestroy,
  ChangeDetectorRef,
} from '@angular/core';
import { ChartConfiguration, ChartData } from 'chart.js';
import { VnstatService } from '../../services/vnstat.service';
import { ThemeService } from '../../services/theme.service';
import { StatsResponse, StatsDataPoint } from '../../models/vnstat.model';
import { Subject, takeUntil } from 'rxjs';

@Component({
  selector: 'app-daily-chart',
  templateUrl: './daily-chart.component.html',
  styleUrls: ['./daily-chart.component.scss'],
  standalone: false,
})
export class DailyChartComponent implements OnInit, OnChanges, OnDestroy {
  @Input() interfaceId: number | null = null;

  totalChartData: ChartData<'bar'> = {
    labels: [],
    datasets: [],
  };

  rxChartData: ChartData<'line'> = {
    labels: [],
    datasets: [],
  };

  txChartData: ChartData<'line'> = {
    labels: [],
    datasets: [],
  };

  chartOptions: ChartConfiguration['options'];
  private destroy$ = new Subject<void>();

  loading = false;
  error: string | null = null;

  constructor(
    private vnstatService: VnstatService,
    private themeService: ThemeService,
    private cdr: ChangeDetectorRef
  ) {
    this.chartOptions = this.getChartOptions();
  }

  private getChartOptions(): ChartConfiguration['options'] {
    const isDark =
      this.themeService.getTheme() === 'dark' ||
      (this.themeService.getTheme() === 'system' &&
        window.matchMedia('(prefers-color-scheme: dark)').matches);

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
  }

  ngOnDestroy(): void {
    this.destroy$.next();
    this.destroy$.complete();
  }

  ngOnInit(): void {
    this.loadData();

    // Listen for theme changes
    this.themeService
      .onThemeChange()
      .pipe(takeUntil(this.destroy$))
      .subscribe(() => {
        this.chartOptions = this.getChartOptions();
      });
  }

  ngOnChanges(changes: SimpleChanges): void {
    if (changes['interfaceId']) {
      this.loadData();
    }
  }

  loadData(): void {
    this.loading = true;
    this.error = null;

    this.vnstatService.getDailyStats(this.interfaceId ?? undefined, 30).subscribe({
      next: (responses: StatsResponse[]) => {
        this.processData(responses);
        this.loading = false;
        this.cdr.detectChanges();
      },
      error: err => {
        this.error = 'Failed to load daily data';
        this.loading = false;
        console.error('Error loading daily data:', err);
        this.cdr.detectChanges();
      },
    });
  }

  private processData(responses: StatsResponse[]): void {
    if (responses.length === 0 || !responses[0]?.data || responses[0].data.length === 0) {
      this.totalChartData = { labels: [], datasets: [] };
      this.rxChartData = { labels: [], datasets: [] };
      this.txChartData = { labels: [], datasets: [] };
      return;
    }

    const labels = responses[0].data.map((point: StatsDataPoint) => {
      const date = new Date(point.timestamp);
      return date.toLocaleDateString();
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

    this.totalChartData = { labels, datasets: totalDatasets };
    this.rxChartData = { labels, datasets: rxDatasets };
    this.txChartData = { labels, datasets: txDatasets };
    this.cdr.markForCheck();
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
