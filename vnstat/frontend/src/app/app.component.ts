import { Component } from '@angular/core';

@Component({
  selector: 'app-root',
  template: `
    <div class="min-h-screen bg-gray-50 dark:bg-gray-900 transition-colors duration-300">
      <app-dashboard></app-dashboard>
    </div>
  `,
  styles: [],
  standalone: false,
})
export class AppComponent {
  title = 'VNStat Dashboard';
}
