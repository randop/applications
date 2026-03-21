import { bootstrapApplication } from '@angular/platform-browser';
import { provideRouter } from '@angular/router';
import { provideHttpClient } from '@angular/common/http';
import { provideAnimations } from '@angular/platform-browser/animations';
import { provideCharts, withDefaultRegisterables } from 'ng2-charts';
import { ApplicationConfig } from '@angular/core';

import { AppComponent } from './app/app.component';
import { DashboardComponent } from './app/components/dashboard/dashboard.component';

const appConfig: ApplicationConfig = {
  providers: [
    provideRouter([
      { path: '', component: DashboardComponent },
      { path: '**', redirectTo: '' },
    ]),
    provideHttpClient(),
    provideAnimations(),
    provideCharts(withDefaultRegisterables()),
  ],
};

bootstrapApplication(AppComponent, appConfig).catch(err => console.error(err));
