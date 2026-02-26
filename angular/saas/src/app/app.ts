import { Component, signal } from '@angular/core';
import { NgIcon, provideIcons } from '@ng-icons/core';
import * as radixIcons from '@ng-icons/radix-icons';
import { RouterOutlet } from '@angular/router';
import { UbButtonDirective } from '~/components/ui/button';

@Component({
  selector: 'app-root',
  imports: [RouterOutlet, NgIcon, UbButtonDirective],
  providers: [provideIcons(radixIcons)],
  templateUrl: './app.html',
  styleUrl: './app.css',
})
export class App {
  protected readonly title = signal('saas');
}
