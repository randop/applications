import { Injectable, Inject, Renderer2, RendererFactory2, signal, computed } from '@angular/core';
import { DOCUMENT } from '@angular/common';

export type Theme = 'system' | 'light' | 'dark';

@Injectable({
  providedIn: 'root',
})
export class ThemeService {
  private renderer: Renderer2;
  private mediaQuery: MediaQueryList | null = null;

  private themeState = signal<Theme>('system');

  readonly currentTheme = computed(() => this.themeState());

  readonly isDark = computed(() => {
    const theme = this.themeState();
    if (theme === 'dark') {
      return true;
    } else if (theme === 'system') {
      return window.matchMedia('(prefers-color-scheme: dark)').matches;
    }
    return false;
  });

  constructor(
    @Inject(DOCUMENT) private document: Document,
    rendererFactory: RendererFactory2
  ) {
    this.renderer = rendererFactory.createRenderer(null, null);
    this.initTheme();
  }

  private initTheme(): void {
    const savedTheme = localStorage.getItem('theme') as Theme;
    if (savedTheme && ['system', 'light', 'dark'].includes(savedTheme)) {
      this.setTheme(savedTheme);
    } else {
      this.setTheme('system');
    }
  }

  setTheme(theme: Theme): void {
    // Update signal
    this.themeState.set(theme);

    localStorage.setItem('theme', theme);

    this.renderer.removeClass(this.document.documentElement, 'dark');
    this.renderer.removeClass(this.document.documentElement, 'light');

    if (theme === 'system') {
      this.mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
      this.applySystemTheme(this.mediaQuery);

      this.mediaQuery.addEventListener('change', e => this.applySystemTheme(e));
    } else {
      if (this.mediaQuery) {
        this.mediaQuery.removeEventListener('change', e => this.applySystemTheme(e));
        this.mediaQuery = null;
      }

      this.renderer.addClass(this.document.documentElement, theme);
    }
  }

  private applySystemTheme(mediaQuery: MediaQueryList | MediaQueryListEvent): void {
    if (mediaQuery.matches) {
      this.renderer.addClass(this.document.documentElement, 'dark');
    } else {
      this.renderer.addClass(this.document.documentElement, 'light');
    }
  }

  // Deprecated: use currentTheme() signal instead
  getTheme(): Theme {
    return this.themeState();
  }

  // Deprecated: use isDark() signal instead
  isDarkMode(): boolean {
    return this.isDark();
  }

  toggleTheme(): void {
    const themes: Theme[] = ['system', 'light', 'dark'];
    const currentIndex = themes.indexOf(this.themeState());
    const nextIndex = (currentIndex + 1) % themes.length;
    this.setTheme(themes[nextIndex]);
  }
}
