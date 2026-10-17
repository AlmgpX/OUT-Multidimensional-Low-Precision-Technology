// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
using UnityEngine;



namespace OUT.Motor

{

    /// <summary>Shared grid + two goal distance fields. O(cells), not one NavMeshAgent per zombie.</summary>

    public sealed class OUTMotorFlowField

    {

        private readonly int width, cells;

        private readonly float cell;

        private readonly Vector2 origin;

        private readonly byte[] blocked;

        private readonly ushort[] toTeam0, toTeam1;

        private readonly int[] queue;

        private static readonly int[] OffX={1,0,-1,0,1,-1,1,-1};

        private static readonly int[] OffY={0,1,0,-1,1,1,-1,-1};

        public int Width { get { return width; } }

        public int BlockedCount { get; private set; }



        public OUTMotorFlowField(int gridWidth,float cellSize,Vector2 gridCentre)

        {

            width=Mathf.Clamp(gridWidth,16,128);cells=width*width;cell=Mathf.Max(.5f,cellSize);

            origin=gridCentre-new Vector2(width*cell*.5f,width*cell*.5f);

            blocked=new byte[cells];toTeam0=new ushort[cells];toTeam1=new ushort[cells];queue=new int[cells];

        }

        private int Index(int x,int y) { return y*width+x; }

        public Vector2Int Cell(Vector3 p)

        {

            int x=Mathf.Clamp(Mathf.FloorToInt((p.x-origin.x)/cell),0,width-1);

            int y=Mathf.Clamp(Mathf.FloorToInt((p.z-origin.y)/cell),0,width-1);

            return new Vector2Int(x,y);

        }

        public Vector3 Centre(int x,int y) {return new Vector3(origin.x+(x+.5f)*cell,0,origin.y+(y+.5f)*cell);}

        public void Bake(LayerMask layerMask)

        {

            BlockedCount=0;

            if(layerMask.value==0){System.Array.Clear(blocked,0,blocked.Length);return;}

            Vector3 half = new Vector3(cell*.38f,1f,cell*.38f);

            for(int y=0;y<width;y++)for(int x=0;x<width;x++)

            {

                bool b=Physics.CheckBox(Centre(x,y)+Vector3.up,half,Quaternion.identity,layerMask,QueryTriggerInteraction.Ignore);

                blocked[Index(x,y)]=(byte)(b?1:0);

                if(b)BlockedCount++;

            }

        }

        public void Rebuild(Vector3 goalTeam0,Vector3 goalTeam1)

        {

            Flood(Cell(goalTeam0),toTeam0);Flood(Cell(goalTeam1),toTeam1);

        }

        private void Flood(Vector2Int goal, ushort[] distances)

        {

            for(int i=0;i<cells;i++)distances[i]=ushort.MaxValue;

            int x=goal.x,y=goal.y;

            if(blocked[Index(x,y)]!=0)

            {

                bool found=false;

                for(int radius=1;radius<width/2&&!found;radius++)

                for(int j=-radius;j<=radius&&!found;j++)

                for(int k=-radius;k<=radius&&!found;k++)

                {

                    int xx=x+k,yy=y+j;

                    if(xx>=0&&yy>=0&&xx<width&&yy<width&&blocked[Index(xx,yy)]==0)

                    { x=xx;y=yy;found=true; }

                }

                if(!found)return;

            }

            int first=0,last=0,start=Index(x,y);

            queue[last++]=start;distances[start]=0;

            while(first<last)

            {

                int n=queue[first++],cy=n/width,cx=n%width;

                ushort dist=distances[n];

                for(int i=0;i<4;i++)

                {

                    int xx=cx+OffX[i],yy=cy+OffY[i];

                    if(xx<0||yy<0||xx>=width||yy>=width)continue;

                    int idx=Index(xx,yy);

                    if(blocked[idx]!=0||distances[idx]!=ushort.MaxValue)continue;

                    distances[idx]=(ushort)(dist+1);

                    queue[last++]=idx;

                }

            }

        }

        public Vector3 Direction(Vector3 position,int goalTeam)

        {

            Vector2Int p=Cell(position);ushort[] dist=goalTeam==0?toTeam0:toTeam1;

            int best=Index(p.x,p.y);ushort bestDist=dist[best];Vector3 bestDir=Vector3.zero;

            for(int i=0;i<8;i++)

            {

                int x=p.x+OffX[i],y=p.y+OffY[i];

                if(x<0||y<0||x>=width||y>=width)continue;

                int idx=Index(x,y);if(blocked[idx]!=0 || dist[idx]>=bestDist)continue;

                if(i>=4)

                {

                    if(blocked[Index(x,p.y)]!=0 || blocked[Index(p.x,y)]!=0)continue;

                }

                Vector3 direction=Centre(x,y)-position;direction.y=0;

                if(direction.sqrMagnitude>.0001f) {bestDir=direction.normalized;bestDist=dist[idx];}

            }

            return bestDir;

        }

        public bool IsBlocked(Vector3 p)

        {

            Vector2Int c=Cell(p);return blocked[Index(c.x,c.y)]!=0;

        }

    }

}
