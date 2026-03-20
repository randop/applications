import { Component, OnInit } from '@angular/core';
import { Interface } from '../../models/vnstat.model';
import { VnstatService } from '../../services/vnstat.service';

@Component({
  selector: 'app-dashboard',
  templateUrl: './dashboard.component.html',
  styleUrls: ['./dashboard.component.scss'],
  standalone: false
})
export class DashboardComponent implements OnInit {
  interfaces: Interface[] = [];
  selectedInterface: number | null = null;
  activeTab: 'hourly' | 'daily' | 'monthly' = 'hourly';

  constructor(private vnstatService: VnstatService) { }

  ngOnInit(): void {
    this.loadInterfaces();
  }

  loadInterfaces(): void {
    this.vnstatService.getInterfaces().subscribe({
      next: (interfaces) => {
        this.interfaces = interfaces;
        // selectedInterface remains null to show "All Interfaces" by default
      },
      error: (error) => {
        console.error('Error loading interfaces:', error);
      }
    });
  }

  onInterfaceChange(event: Event): void {
    const target = event.target as HTMLSelectElement;
    this.selectedInterface = target.value ? parseInt(target.value, 10) : null;
  }

  setActiveTab(tab: 'hourly' | 'daily' | 'monthly'): void {
    this.activeTab = tab;
  }
}
