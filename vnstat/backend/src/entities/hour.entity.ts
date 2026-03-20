import { Entity, PrimaryGeneratedColumn, Column, ManyToOne, JoinColumn } from 'typeorm';
import { Interface } from './interface.entity';

@Entity('hour')
export class Hour {
  @PrimaryGeneratedColumn()
  id: number;

  @Column()
  interface: number;

  @Column()
  date: Date;

  @Column()
  rx: number;

  @Column()
  tx: number;
}
