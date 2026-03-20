import { Component, OnInit, HostListener, ElementRef } from '@angular/core';
import { ThemeService, Theme } from '../../services/theme.service';

@Component({
  selector: 'app-theme-toggle',
  template: `
    <div class="relative" #dropdownContainer>
      <button
        (click)="toggleDropdown($event)"
        class="p-2 rounded-lg bg-gray-200 dark:bg-gray-700 text-gray-700 dark:text-gray-300 hover:bg-gray-300 dark:hover:bg-gray-600 transition-colors duration-200 flex items-center gap-2"
        aria-label="Toggle theme"
      >
        @switch (currentTheme) {
          @case ('light') {
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path
                stroke-linecap="round"
                stroke-linejoin="round"
                stroke-width="2"
                d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z"
              ></path>
            </svg>
          }
          @case ('dark') {
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path
                stroke-linecap="round"
                stroke-linejoin="round"
                stroke-width="2"
                d="M20.354 15.354A9 9 0 018.646 3.646 9.003 9.003 0 0012 21a9.003 9.003 0 008.354-5.646z"
              ></path>
            </svg>
          }
          @default {
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path
                stroke-linecap="round"
                stroke-linejoin="round"
                stroke-width="2"
                d="M9.75 17L9 20l-1 1h8l-1-1-.75-3M3 13h18M5 17h14a2 2 0 002-2V5a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z"
              ></path>
            </svg>
          }
        }
        <span class="text-sm font-medium hidden sm:inline">{{ getThemeLabel() }}</span>
        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
          <path
            stroke-linecap="round"
            stroke-linejoin="round"
            stroke-width="2"
            d="M19 9l-7 7-7-7"
          ></path>
        </svg>
      </button>

      @if (isOpen) {
        <div
          class="absolute right-0 mt-2 w-44 bg-white dark:bg-gray-800 rounded-lg shadow-lg border border-gray-200 dark:border-gray-700 py-1 z-50"
        >
          @for (theme of themes; track theme.value) {
            <button
              (click)="setTheme(theme.value); $event.stopPropagation()"
              class="w-full px-4 py-2.5 text-left text-sm hover:bg-gray-100 dark:hover:bg-gray-700 flex items-center gap-3 transition-colors"
              [class.text-blue-600]="currentTheme === theme.value"
              [class.dark:text-blue-400]="currentTheme === theme.value"
              [class.text-gray-700]="currentTheme !== theme.value"
              [class.dark:text-gray-300]="currentTheme !== theme.value"
            >
              <span class="w-5 h-5 flex items-center justify-center">
                @if (currentTheme === theme.value) {
                  <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                    <path
                      stroke-linecap="round"
                      stroke-linejoin="round"
                      stroke-width="2"
                      d="M5 13l4 4L19 7"
                    ></path>
                  </svg>
                }
              </span>
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                @switch (theme.value) {
                  @case ('light') {
                    <path
                      stroke-linecap="round"
                      stroke-linejoin="round"
                      stroke-width="2"
                      d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z"
                    ></path>
                  }
                  @case ('dark') {
                    <path
                      stroke-linecap="round"
                      stroke-linejoin="round"
                      stroke-width="2"
                      d="M20.354 15.354A9 9 0 018.646 3.646 9.003 9.003 0 0012 21a9.003 9.003 0 008.354-5.646z"
                    ></path>
                  }
                  @default {
                    <path
                      stroke-linecap="round"
                      stroke-linejoin="round"
                      stroke-width="2"
                      d="M9.75 17L9 20l-1 1h8l-1-1-.75-3M3 13h18M5 17h14a2 2 0 002-2V5a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z"
                    ></path>
                  }
                }
              </svg>
              <span class="flex-1">{{ theme.label }}</span>
            </button>
          }
        </div>
      }
    </div>
  `,
  styles: [],
  standalone: false,
})
export class ThemeToggleComponent implements OnInit {
  currentTheme: Theme = 'system';
  isOpen = false;

  themes = [
    { value: 'system' as Theme, label: 'System' },
    { value: 'light' as Theme, label: 'Light' },
    { value: 'dark' as Theme, label: 'Dark' },
  ];

  constructor(
    private themeService: ThemeService,
    private elementRef: ElementRef
  ) {}

  ngOnInit(): void {
    this.currentTheme = this.themeService.getTheme();
  }

  @HostListener('document:click', ['$event'])
  onDocumentClick(event: MouseEvent): void {
    if (!this.elementRef.nativeElement.contains(event.target)) {
      this.isOpen = false;
    }
  }

  toggleDropdown(event: MouseEvent): void {
    event.stopPropagation();
    this.isOpen = !this.isOpen;
  }

  setTheme(theme: Theme): void {
    this.themeService.setTheme(theme);
    this.currentTheme = theme;
    this.isOpen = false;
  }

  getThemeLabel(): string {
    const theme = this.themes.find(t => t.value === this.currentTheme);
    return theme?.label || 'System';
  }
}
