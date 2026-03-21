import { Component, OnInit, signal } from '@angular/core';
import { CommonModule } from '@angular/common';
import { Interface } from '../../models/vnstat.model';
import { VnstatService } from '../../services/vnstat.service';
import { HourlyChartComponent } from '../hourly-chart/hourly-chart.component';
import { DailyChartComponent } from '../daily-chart/daily-chart.component';
import { MonthlyChartComponent } from '../monthly-chart/monthly-chart.component';
import { ThemeToggleComponent } from '../theme-toggle/theme-toggle.component';

@Component({
  selector: 'app-dashboard',
  templateUrl: './dashboard.component.html',
  styleUrls: ['./dashboard.component.scss'],
  standalone: true,
  imports: [
    CommonModule,
    HourlyChartComponent,
    DailyChartComponent,
    MonthlyChartComponent,
    ThemeToggleComponent,
  ],
})
export class DashboardComponent implements OnInit {
  interfaces = signal<Interface[]>([]);
  selectedInterface = signal<number | null>(null);
  activeTab = signal<'hourly' | 'daily' | 'monthly'>('hourly');

  constructor(private vnstatService: VnstatService) {}

  ngOnInit(): void {
    this.loadInterfaces();
  }

  loadInterfaces(): void {
    this.vnstatService.getInterfaces().subscribe({
      next: interfaces => {
        this.interfaces.set(interfaces);
      },
      error: error => {
        console.error('Error loading interfaces:', error);
      },
    });
  }

  onInterfaceChange(event: Event): void {
    const target = event.target as HTMLSelectElement;
    const newValue = target.value ? parseInt(target.value, 10) : null;
    this.selectedInterface.set(newValue);
  }

  setActiveTab(tab: 'hourly' | 'daily' | 'monthly'): void {
    this.activeTab.set(tab);
  }
}
