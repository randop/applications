import { Component } from '@angular/core';

@Component({
  selector: 'app-root',
  template: `
    <div class="min-h-screen bg-gray-900">
      <app-dashboard></app-dashboard>
    </div>
  `,
  styles: [],
  standalone: false
})
export class AppComponent {
  title = 'VNStat Dashboard';
}
