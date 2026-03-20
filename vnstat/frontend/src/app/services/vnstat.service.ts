import { Injectable } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { Observable } from 'rxjs';
import { Interface, StatsResponse } from '../models/vnstat.model';

@Injectable({
  providedIn: 'root'
})
export class VnstatService {
  private apiUrl = '/api/vnstat';

  constructor(private http: HttpClient) { }

  getInterfaces(): Observable<Interface[]> {
    return this.http.get<Interface[]>(`${this.apiUrl}/interfaces`);
  }

  getHourlyStats(interfaceId?: number, limit: number = 24, startDate?: string, endDate?: string): Observable<StatsResponse[]> {
    let url = `${this.apiUrl}/hourly`;
    const params: string[] = [];
    
    if (startDate && endDate) {
      params.push(`startDate=${startDate}`);
      params.push(`endDate=${endDate}`);
    } else {
      params.push(`limit=${limit}`);
    }
    
    if (interfaceId) {
      params.push(`interfaceId=${interfaceId}`);
    }
    
    if (params.length > 0) {
      url += '?' + params.join('&');
    }
    
    return this.http.get<StatsResponse[]>(url);
  }

  getDailyStats(interfaceId?: number, limit: number = 30): Observable<StatsResponse[]> {
    let url = `${this.apiUrl}/daily?limit=${limit}`;
    if (interfaceId) {
      url += `&interfaceId=${interfaceId}`;
    }
    return this.http.get<StatsResponse[]>(url);
  }

  getMonthlyStats(interfaceId?: number, limit: number = 12): Observable<StatsResponse[]> {
    let url = `${this.apiUrl}/monthly?limit=${limit}`;
    if (interfaceId) {
      url += `&interfaceId=${interfaceId}`;
    }
    return this.http.get<StatsResponse[]>(url);
  }
}
