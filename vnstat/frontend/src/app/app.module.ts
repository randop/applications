import { NgModule } from '@angular/core';
import { BrowserModule } from '@angular/platform-browser';
import { BrowserAnimationsModule } from '@angular/platform-browser/animations';
import { provideHttpClient } from '@angular/common/http';
import { provideCharts, withDefaultRegisterables, BaseChartDirective } from 'ng2-charts';

import { AppRoutingModule } from './app-routing.module';
import { AppComponent } from './app.component';
import { DashboardComponent } from './components/dashboard/dashboard.component';
import { HourlyChartComponent } from './components/hourly-chart/hourly-chart.component';
import { DailyChartComponent } from './components/daily-chart/daily-chart.component';
import { MonthlyChartComponent } from './components/monthly-chart/monthly-chart.component';
import { HumanizeBytesPipe } from './pipes/humanize-bytes.pipe';
import { ThemeToggleComponent } from './components/theme-toggle/theme-toggle.component';

@NgModule({
  declarations: [
    AppComponent,
    DashboardComponent,
    HourlyChartComponent,
    DailyChartComponent,
    MonthlyChartComponent,
    HumanizeBytesPipe,
    ThemeToggleComponent,
  ],
  imports: [BrowserModule, BrowserAnimationsModule, BaseChartDirective, AppRoutingModule],
  providers: [provideHttpClient(), provideCharts(withDefaultRegisterables())],
  bootstrap: [AppComponent],
})
export class AppModule {}
