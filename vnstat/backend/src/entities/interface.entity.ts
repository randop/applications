import { Entity, PrimaryGeneratedColumn, Column } from 'typeorm';

@Entity('interface')
export class Interface {
  @PrimaryGeneratedColumn()
  id: number;

  @Column()
  name: string;

  @Column({ nullable: true })
  alias: string;

  @Column()
  active: number;

  @Column()
  created: Date;

  @Column()
  updated: Date;

  @Column()
  rxcounter: number;

  @Column()
  txcounter: number;

  @Column()
  rxtotal: number;

  @Column()
  txtotal: number;
}
