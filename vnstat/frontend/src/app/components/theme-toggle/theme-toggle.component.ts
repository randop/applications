import { Component, HostListener, ElementRef, signal, computed } from '@angular/core';
import { CommonModule } from '@angular/common';
import { ThemeService, Theme } from '../../services/theme.service';

@Component({
  selector: 'app-theme-toggle',
  templateUrl: './theme-toggle.component.html',
  standalone: true,
  imports: [CommonModule],
})
export class ThemeToggleComponent {
  isOpen = signal(false);

  currentTheme = computed(() => this.themeService.currentTheme());

  themeLabel = computed(() => {
    const theme = this.themes.find(t => t.value === this.currentTheme());
    return theme?.label || 'System';
  });

  themes = [
    { value: 'system' as Theme, label: 'System' },
    { value: 'light' as Theme, label: 'Light' },
    { value: 'dark' as Theme, label: 'Dark' },
  ];

  constructor(
    private themeService: ThemeService,
    private elementRef: ElementRef
  ) {}

  @HostListener('document:click', ['$event'])
  onDocumentClick(event: MouseEvent): void {
    if (!this.elementRef.nativeElement.contains(event.target)) {
      this.isOpen.set(false);
    }
  }

  toggleDropdown(event: MouseEvent): void {
    event.stopPropagation();
    this.isOpen.update(open => !open);
  }

  setTheme(theme: Theme): void {
    this.themeService.setTheme(theme);
    this.isOpen.set(false);
  }
}
