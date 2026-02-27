import { Component, signal } from '@angular/core';
import { NgIcon, provideIcons } from '@ng-icons/core';
import * as radixIcons from '@ng-icons/radix-icons';
import { RouterOutlet } from '@angular/router';
import { ZardIconComponent } from '@/shared/components/icon/icon.component';
import { ZardButtonComponent } from '@/shared/components/button/button.component';

@Component({
  selector: 'app-root',
  imports: [RouterOutlet, NgIcon, ZardButtonComponent, ZardIconComponent],
  providers: [provideIcons(radixIcons)],
  templateUrl: './app.html',
  styleUrl: './app.css',
})
export class App {
  protected readonly title = signal('saas');
}
