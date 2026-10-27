import { CommonModule, formatDate } from '@angular/common';
import { HttpClient } from '@angular/common/http';
import { Component, inject, signal } from '@angular/core';
import { ActivatedRoute, RouterLink } from '@angular/router';

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

@Component({
  selector: 'app-anime-detail-page',
  standalone: true,
  imports: [CommonModule, RouterLink],
  template: `
    <div class="shell detail-shell">
      <header class="topbar">
        <a class="brand" routerLink="/" aria-label="Anime Catalog home">
          <span class="brand-mark">A</span><span>ANIME<span class="muted"> ARCHIVE</span></span>
        </a>
        <a class="top-back" routerLink="/">← ALL TITLES</a>
      </header>

      <main class="detail-main">
        <a class="back-nav" routerLink="/"><span>←</span> Back to your collection</a>

        <div *ngIf="loading()" class="state-panel detail-state">
          <span class="spinner dark"></span><p>Loading anime details…</p>
        </div>

        <section *ngIf="!loading() && error()" class="detail-error">
          <div class="empty-icon">?</div>
          <h2>Anime not found</h2>
          <p>{{ error() }}</p>
          <a class="primary-link" routerLink="/">Return to collection</a>
        </section>

        <ng-container *ngIf="!loading() && anime() as item">
          <section class="detail-hero">
            <div class="detail-cover-wrap">
              <img *ngIf="cover(item) as imageUrl" class="detail-cover" [src]="imageUrl" [alt]="coverAlt(item)" fetchpriority="high">
              <div *ngIf="!cover(item)" class="detail-cover detail-cover-empty"><span>✳</span><small>ARTWORK UNAVAILABLE</small></div>
            </div>

            <div class="detail-copy">
              <div class="eyebrow detail-eyebrow">ANIME PROFILE <span>／</span> CATALOG ENTRY</div>
              <h1>{{ item.title }}</h1>
              <p class="detail-native-title" *ngIf="item.originalTitle">{{ item.originalTitle }}</p>
              <p class="detail-alternative" *ngIf="item.alternativeTitle && item.alternativeTitle !== item.originalTitle">{{ item.alternativeTitle }}</p>

              <div class="detail-genres" *ngIf="item.genres.length">
                <span *ngFor="let genre of item.genres">{{ genre }}</span>
              </div>

              <div class="detail-divider"></div>
              <div class="detail-section-label">SYNOPSIS</div>
              <p class="detail-synopsis">{{ item.synopsis || 'No synopsis has been added for this title yet.' }}</p>

              <div class="detail-facts">
                <div class="detail-fact">
                  <span>RELEASE DATE</span>
                  <strong>{{ formatReleaseDate(item.releaseDate) }}</strong>
                </div>
                <div class="detail-fact">
                  <span>ADDED TO LIBRARY</span>
                  <strong>{{ item.createdAt ? (item.createdAt | date:'mediumDate') : 'Unknown' }}</strong>
                </div>
              </div>

              <a class="primary-link source-button" [href]="item.sourceUrl" target="_blank" rel="noopener noreferrer">
                Visit source website <span>↗</span>
              </a>
            </div>
          </section>

          <section class="detail-artwork" *ngIf="otherImages(item).length">
            <div class="artwork-heading">
              <div><div class="eyebrow">MORE FROM THIS TITLE</div><h2>Artwork gallery</h2></div>
              <span class="artwork-count">{{ otherImages(item).length }} IMAGE{{ otherImages(item).length === 1 ? '' : 'S' }}</span>
            </div>
            <div class="artwork-grid">
              <figure class="artwork-card" *ngFor="let image of otherImages(item); trackBy: trackByImage">
                <img [src]="imageUrl(image)" [alt]="image.alt || item.title" loading="lazy">
                <figcaption>{{ image.alt || image.type }}</figcaption>
              </figure>
            </div>
          </section>

          <div class="detail-source-note"><span class="status-dot"></span> METADATA ADDED FROM <a [href]="item.sourceUrl" target="_blank" rel="noopener noreferrer">THE ORIGINAL WEBSITE ↗</a></div>
        </ng-container>

        <div class="footer-note">A SMALL LIBRARY OF BIG WORLDS <span>·</span> ANIME ARCHIVE</div>
      </main>
    </div>
  `,
})
export class AnimeDetailComponent {
  private readonly http = inject(HttpClient);
  private readonly route = inject(ActivatedRoute);
  readonly anime = signal<Anime | null>(null);
  readonly loading = signal(true);
  readonly error = signal('');

  constructor() {
    this.route.paramMap.subscribe((params) => {
      const id = params.get('id');
      if (!id) {
        this.loading.set(false);
        this.error.set('No anime ID was provided.');
        return;
      }
      this.load(id);
    });
  }

  private load(id: string) {
    this.loading.set(true);
    this.error.set('');
    this.anime.set(null);
    this.http.get<Anime>(`/api/anime/${encodeURIComponent(id)}`).subscribe({
      next: (anime) => {
        this.anime.set(anime);
        this.loading.set(false);
      },
      error: (error: { status?: number }) => {
        this.loading.set(false);
        this.error.set(error.status === 404
          ? 'This title may have been removed or the link may be incorrect.'
          : 'The anime details could not be loaded. Check that the API is running and try again.');
      },
    });
  }

  cover(anime: Anime): string | undefined {
    const images = anime.images ?? [];
    const image = images.find((item) => item.type.toLowerCase().includes('cover') || item.type.toLowerCase().includes('key-visual')) ?? images[0];
    return image ? this.imageUrl(image) : undefined;
  }

  coverAlt(anime: Anime) {
    const image = (anime.images ?? []).find((item) => item.type.toLowerCase().includes('cover') || item.type.toLowerCase().includes('key-visual')) ?? anime.images?.[0];
    return image?.alt || `${anime.title} cover`;
  }

  imageUrl(image: AnimeImage): string {
    return image.localPath || image.url;
  }

  otherImages(anime: Anime): AnimeImage[] {
    const main = this.cover(anime);
    return (anime.images ?? []).filter((image) => this.imageUrl(image) !== main);
  }

  trackByImage(index: number, image: AnimeImage) {
    return `${image.id}-${image.url}-${index}`;
  }

  formatReleaseDate(value?: string): string {
    if (!value) {
      return 'Unknown';
    }

    const match = /^(\d{4})(?:-(\d{2})(?:-(\d{2}))?)?$/.exec(value);

    if (!match) {
      return value;
    }

    const [, yearText, monthText, dayText] = match;

    // Year only: 2026
    if (!monthText) {
      return yearText;
    }

    const year = Number(yearText);
    const month = Number(monthText);
    const day = dayText ? Number(dayText) : 1;

    if (month < 1 || month > 12) {
      return value;
    }

    const date = new Date(year, month - 1, day);

    // Reject invalid dates such as February 31.
    if (
      date.getFullYear() !== year ||
      date.getMonth() !== month - 1 ||
      date.getDate() !== day
    ) {
      return value;
    }

    // Full date: Apr 4, 2026
    if (dayText) {
      return formatDate(date, 'mediumDate', 'en-US');
    }

    // Year and month only: Apr 2026
    return formatDate(date, 'MMM yyyy', 'en-US');
  }
}
