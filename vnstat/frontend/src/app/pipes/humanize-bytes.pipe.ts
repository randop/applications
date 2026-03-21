import { Pipe, PipeTransform } from '@angular/core';

@Pipe({
  name: 'humanizeBytes',
  standalone: true,
})
export class HumanizeBytesPipe implements PipeTransform {
  transform(bytes: number, decimals: number = 2): string {
    if (bytes === 0) {
      return '0 B';
    }

    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let size = bytes;
    let unitIndex = 0;

    while (size >= 1024 && unitIndex < units.length - 1) {
      size /= 1024;
      unitIndex++;
    }

    return `${size.toFixed(decimals)} ${units[unitIndex]}`;
  }
}
