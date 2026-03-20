import { Injectable, Inject, Renderer2, RendererFactory2 } from '@angular/core';
import { DOCUMENT } from '@angular/common';
import { Subject, Observable } from 'rxjs';

export type Theme = 'system' | 'light' | 'dark';

@Injectable({
  providedIn: 'root',
})
export class ThemeService {
  private renderer: Renderer2;
  private currentTheme: Theme = 'system';
  private mediaQuery: MediaQueryList | null = null;
  private themeChangeSubject = new Subject<void>();

  constructor(
    @Inject(DOCUMENT) private document: Document,
    rendererFactory: RendererFactory2
  ) {
    this.renderer = rendererFactory.createRenderer(null, null);
    this.initTheme();
  }

  private initTheme(): void {
    // Check for saved preference
    const savedTheme = localStorage.getItem('theme') as Theme;
    if (savedTheme && ['system', 'light', 'dark'].includes(savedTheme)) {
      this.setTheme(savedTheme);
    } else {
      this.setTheme('system');
    }
  }

  onThemeChange(): Observable<void> {
    return this.themeChangeSubject.asObservable();
  }

  setTheme(theme: Theme): void {
    this.currentTheme = theme;
    localStorage.setItem('theme', theme);

    // Remove existing theme classes
    this.renderer.removeClass(this.document.documentElement, 'dark');
    this.renderer.removeClass(this.document.documentElement, 'light');

    if (theme === 'system') {
      // Listen to system preference
      this.mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
      this.applySystemTheme(this.mediaQuery);

      // Add listener for changes
      this.mediaQuery.addEventListener('change', e => this.applySystemTheme(e));
    } else {
      // Remove system listener if exists
      if (this.mediaQuery) {
        this.mediaQuery.removeEventListener('change', e => this.applySystemTheme(e));
        this.mediaQuery = null;
      }

      // Apply chosen theme
      this.renderer.addClass(this.document.documentElement, theme);
    }

    // Notify subscribers of theme change
    this.themeChangeSubject.next();
  }

  private applySystemTheme(mediaQuery: MediaQueryList | MediaQueryListEvent): void {
    if (mediaQuery.matches) {
      this.renderer.addClass(this.document.documentElement, 'dark');
    } else {
      this.renderer.addClass(this.document.documentElement, 'light');
    }
  }

  getTheme(): Theme {
    return this.currentTheme;
  }

  isDarkMode(): boolean {
    if (this.currentTheme === 'dark') {
      return true;
    } else if (this.currentTheme === 'system') {
      return window.matchMedia('(prefers-color-scheme: dark)').matches;
    }
    return false;
  }

  toggleTheme(): void {
    const themes: Theme[] = ['system', 'light', 'dark'];
    const currentIndex = themes.indexOf(this.currentTheme);
    const nextIndex = (currentIndex + 1) % themes.length;
    this.setTheme(themes[nextIndex]);
  }
}
