import { bootstrapApplication } from '@angular/platform-browser';
import { provideHttpClient } from '@angular/common/http';
import { provideRouter, Routes } from '@angular/router';
import { AppComponent } from './app/app.component';
import { CatalogPageComponent } from './app/catalog-page.component';
import { AnimeDetailComponent } from './app/anime-detail.component';

const routes: Routes = [
  { path: '', pathMatch: 'full', component: CatalogPageComponent, title: 'Anime Archive' },
  { path: 'anime/:id', component: AnimeDetailComponent, title: 'Anime Details' },
  { path: '**', redirectTo: '' },
];

bootstrapApplication(AppComponent, {
  providers: [provideHttpClient(), provideRouter(routes)],
}).catch((error) => console.error(error));
