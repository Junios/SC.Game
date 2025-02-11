﻿using System;
using System.Collections.Generic;
using System.Text;
using System.Numerics;
using SC.Game;

namespace Project_SLOW
{
	class TargetPositionMove : Behaviour
	{
		Animator animator;
		CharacterController cc;

		Vector3 target;
		bool targetAccepted = false;
		float gravityAccel = 0.0f;

		public TargetPositionMove() : base()
		{

		}

		public override void Start()
		{
			animator = GetComponentInChildren<Animator>();
			cc = GetComponentInChildren<CharacterController>();

			target = Transform.LocalPosition;
		}

		public override void FixedUpdate()
		{
			Vector3 disp = Transform.Position;
			BasePage.Debug = disp;

			if ( targetAccepted )
			{
				var vec = target - disp;
				vec.Y = 0.0f;

				if ( vec.Length() >= 0.1f )
				{
					// 원래의 벡터와 회전 이후 벡터를 계산합니다.
					var fromV = Transform.Forward;
					var toV = Vector3.Normalize( vec );

					// 대상 지점까지의 거리를 계산합니다.
					var distance = Vector3.Distance( disp, target );

					// 거리만큼 이동합니다.
					var move = toV * Math.Min( speed * Time.FixedDeltaTime, distance );
					disp += move;

					// 대상 방향의 회전 각도를 계산합니다.
					var dot = Vector3.Dot( fromV, toV );
					var axis = Vector3.Cross( fromV, toV );
					var angle = ( float )Math.Acos( dot );

					// 회전 각도를 제한합니다.
					if ( angle >= 0.01f )
					{
						angle = Math.Min( Time.FixedDeltaTime * rotateSpeed, angle );

						if ( axis.Length() < 0.05f )
						{
							axis = Vector3.UnitY;
						}

						axis.X = 0;
						axis.Z = 0;

						axis = Vector3.Normalize( axis );
						Transform.Rotation = Quaternion.CreateFromAxisAngle( axis, angle ) * Transform.Rotation;

						animator.SetVar( "walkSpeed", 1.0f );
					}
				}
				else
				{
					animator.SetVar( "walkSpeed", 0.0f );
					targetAccepted = false;
				}
			}

			gravityAccel += Time.FixedDeltaTime * 9.8f;
			disp.Y -= gravityAccel * Time.FixedDeltaTime;

			var flag = cc.MovePosition( disp );
			if ( ( flag & CharacterCollisionFlags.Down ) == CharacterCollisionFlags.Down )
			{
				gravityAccel = 0;
			}
		}

		public Vector3 Target
		{
			get
			{
				if ( targetAccepted )
				{
					return Transform.Position;
				}
				else
				{
					return target;
				}
			}
			set
			{
				target = value;
				targetAccepted = true;
			}
		}

		public float speed = 3.0f;
		public float rotateSpeed = 15.0f;
	}
}
