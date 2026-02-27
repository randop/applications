import { Component, signal, ChangeDetectionStrategy } from '@angular/core';
import { NgIcon, provideIcons } from '@ng-icons/core';
import * as radixIcons from '@ng-icons/radix-icons';
import { RouterOutlet } from '@angular/router';
import { ZardIconComponent } from '@/shared/components/icon/icon.component';
import { ZardButtonComponent } from '@/shared/components/button/button.component';
import { ZardAlertComponent } from '@/shared/components/alert/alert.component';
import { ZardAvatarGroupComponent } from '@/shared/components/avatar/avatar-group.component';
import { ZardAvatarComponent } from '@/shared/components/avatar/avatar.component';
import { ZardBadgeComponent } from '@/shared/components/badge/badge.component';
import { ZardAccordionImports } from '@/shared/components/accordion/accordion.imports';
import { ZardCardComponent } from '@/shared/components/card/card.component';
import { ZardIdDirective } from '@/shared/core';
import { ZardCheckboxComponent } from '@/shared/components/checkbox/checkbox.component';
import { ZardDividerComponent } from '@/shared/components/divider/divider.component';
import { FormsModule } from '@angular/forms';

@Component({
  selector: 'app-root',
  imports: [
    RouterOutlet,
    NgIcon,
    ZardAccordionImports,
    ZardAvatarComponent,
    ZardAvatarGroupComponent,
    ZardBadgeComponent,
    ZardButtonComponent,
    ZardCardComponent,
    ZardIdDirective,
    ZardIconComponent,
    ZardAlertComponent,
    ZardCheckboxComponent,
    ZardDividerComponent,
    FormsModule,
  ],
  providers: [provideIcons(radixIcons)],
  templateUrl: './app.html',
  styleUrl: './app.css',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class App {
  protected readonly title = signal('saas');

  checked = true;

  protected onActionClick(): void {
    alert('Redirect to Sign Up');
  }
}
