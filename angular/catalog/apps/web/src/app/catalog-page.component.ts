import { CommonModule } from '@angular/common';
import { HttpClient, HttpErrorResponse } from '@angular/common/http';
import { Component, inject, signal } from '@angular/core';
import { Router, RouterLink } from '@angular/router';
import { FormsModule } from '@angular/forms';

interface AnimeImage { id: string; type: string; url: string; alt: string; localPath?: string; }
interface Anime {
  id: string;
  title: string;
  originalTitle?: string;
  alternativeTitle?: string;
  releaseDate?: string;
  genres: string[];
  images: AnimeImage[];
  synopsis: string;
  sourceUrl: string;
  createdAt: string;
}
interface AnimeListResponse { items: Anime[]; total: number; limit: number; skip: number; }

@Component({
  selector: 'app-catalog-page',
  standalone: true,
  imports: [CommonModule, FormsModule, RouterLink],
  template: `
    <div class="shell">
      <header class="topbar">
        <a class="brand" routerLink="/" aria-label="Anime Catalog home">
          <span class="brand-mark">A</span><span>ANIME<span class="muted"> ARCHIVE</span></span>
        </a>
        <div class="top-meta"><span class="status-dot"></span> PERSONAL COLLECTION</div>
      </header>

      <main>
        <section class="hero">
          <form class="import-panel" (ngSubmit)="importAnime()">
            <div class="field-label">IMPORT FROM WEBSITE</div>
            <div class="import-row">
              <input name="sourceUrl" [(ngModel)]="sourceUrl" type="url" placeholder="https://example.com/anime/title" required aria-label="Anime website URL">
              <button type="submit" [disabled]="importing() || !sourceUrl.trim()">
                <span *ngIf="!importing()">＋ Import anime</span>
                <span *ngIf="importing()" class="loading-label"><span class="spinner"></span> Extracting…</span>
              </button>
            </div>
            <div class="hint">AI extracts the title, synopsis, genres, dates and artwork.</div>
            <div *ngIf="message()" class="notice" [class.error]="isError()">{{ message() }}</div>
          </form>
        </section>

        <section class="library">
          <div class="section-head">
            <div><div class="eyebrow">THE LIBRARY</div><h2>Your collection <span class="count">{{ total() }}</span></h2></div>
            <div class="search-wrap"><span>⌕</span><input [(ngModel)]="searchText" (ngModelChange)="search()" placeholder="Search your collection…" aria-label="Search collection"></div>
          </div>

          <div *ngIf="loading()" class="state-panel"><span class="spinner dark"></span><p>Loading your collection…</p></div>
          <div *ngIf="!loading() && items().length === 0" class="empty-state">
            <div class="empty-icon">▧</div>
            <h3>Your next favorite starts here</h3>
            <p>Import an anime from a website to start building your library.</p>
          </div>

          <div class="anime-grid" *ngIf="!loading() && items().length > 0">
            <article class="anime-card" *ngFor="let anime of items(); trackBy: trackById" role="link" tabindex="0" [attr.aria-label]="'View details for ' + anime.title" (click)="openAnime(anime)" (keydown.enter)="openAnime(anime)" (keydown.space)="$event.preventDefault(); openAnime(anime)">
              <div class="poster">
                <img *ngIf="cover(anime) as imageUrl" [src]="imageUrl" [alt]="coverAlt(anime)" loading="lazy">
                <div *ngIf="!cover(anime)" class="poster-placeholder"><span>✳</span><small>ARTWORK UNAVAILABLE</small></div>
                <span class="release" *ngIf="anime.releaseDate">{{ anime.releaseDate.slice(0, 4) }}</span>
              </div>
              <div class="card-content">
                <div class="genre-list"><span *ngFor="let genre of anime.genres.slice(0, 3)">{{ genre }}</span></div>
                <h3>{{ anime.title }}</h3>
                <p class="alt-title" *ngIf="anime.alternativeTitle || anime.originalTitle">{{ anime.alternativeTitle || anime.originalTitle }}</p>
                <p class="synopsis">{{ anime.synopsis || 'No synopsis available yet.' }}</p>
                <a class="source-link" [href]="anime.sourceUrl" target="_blank" rel="noopener noreferrer" (click)="$event.stopPropagation()" (keydown)="$event.stopPropagation()">SOURCE WEBSITE ↗</a>
              </div>
            </article>
          </div>
          <div class="footer-note">A SMALL LIBRARY OF BIG WORLDS <span>·</span> {{ total() }} TITLES</div>
        </section>
      </main>
    </div>
  `,
})
export class CatalogPageComponent {
  private readonly http = inject(HttpClient);
  private readonly router = inject(Router);
  private readonly api = '/api/anime';
  sourceUrl = '';
  searchText = '';
  readonly items = signal<Anime[]>([]);
  readonly total = signal(0);
  readonly loading = signal(true);
  readonly importing = signal(false);
  readonly message = signal('');
  readonly isError = signal(false);
  private searchTimer?: ReturnType<typeof setTimeout>;

  constructor() { this.load(); }

  load(q = '') {
    this.loading.set(true);
    this.http.get<AnimeListResponse>(this.api, { params: { q, limit: 60 } }).subscribe({
      next: (response) => { this.items.set(response.items); this.total.set(response.total); this.loading.set(false); },
      error: () => { this.loading.set(false); this.message.set('Could not connect to the API. Make sure the NestJS server is running.'); this.isError.set(true); },
    });
  }

  search() {
    clearTimeout(this.searchTimer);
    this.searchTimer = setTimeout(() => this.load(this.searchText), 250);
  }

  importAnime() {
    if (!this.sourceUrl.trim() || this.importing()) return;
    this.importing.set(true); this.message.set(''); this.isError.set(false);
    this.http.post<Anime>(`${this.api}/import`, { url: this.sourceUrl.trim() }).subscribe({
      next: (anime) => {
        this.importing.set(false); this.sourceUrl = '';
        this.message.set(`Imported “${anime.title}” successfully.`);
        this.isError.set(false); this.searchText = ''; this.load();
      },
      error: (error: HttpErrorResponse) => {
        this.importing.set(false);
        const message = error.error?.message;
        this.message.set(typeof message === 'string' ? message : 'Import failed. Check the URL, AI configuration and API logs.');
        this.isError.set(true);
      },
    });
  }

  openAnime(anime: Anime) {
    void this.router.navigate(['/anime', anime.id]);
  }

  cover(anime: Anime): string | undefined {
    const image = anime.images?.find((item) => item.type.toLowerCase().includes('cover') || item.type.toLowerCase().includes('key-visual')) ?? anime.images?.[0];
    if (!image) return undefined;
    if (image.localPath) return image.localPath;
    return image.url;
  }
  coverAlt(anime: Anime) { return anime.images?.[0]?.alt || `${anime.title} cover`; }
  trackById(_index: number, anime: Anime) { return anime.id; }
}
